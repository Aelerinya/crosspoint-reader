#include "CrossPointWebServerActivity.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <WiFi.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "NetworkModeSelectionActivity.h"
#include "SilentRestart.h"
#include "WifiSelectionActivity.h"
#include "activities/network/CalibreConnectActivity.h"
#include "components/Sprite.h"
#include "components/UITheme.h"
#include "components/themes/extra/DeckList.h"
#include "components/themes/extra/DeckScreens.h"
#include "components/themes/extra/DeckTheme.h"
#include "fontIds.h"
#include "util/PluginEvents.h"
#include "util/QrUtils.h"
#include "util/TaskWatchdog.h"

namespace {
// AP Mode configuration
constexpr const char* AP_SSID = "CrossPoint-Reader";
constexpr const char* AP_PASSWORD = nullptr;  // Open network for ease of use
constexpr const char* AP_HOSTNAME = "crosspoint";
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CONNECTIONS = 4;
constexpr int QR_CODE_WIDTH = 198;
constexpr int QR_CODE_HEIGHT = 198;

// Upload progress is sampled at most every 250 ms (the HTTP hook runs on every
// chunk). Redraws are at least 2 s apart, 4 s for progress alone, so the e-ink
// refresh (which shares the SPI bus with the SD card) cannot crowd the upload,
// even for a run of small files. Changes inside the gap are drawn when it ends.
constexpr unsigned long TRANSFER_POLL_MS = 250;
constexpr unsigned long TRANSFER_MIN_GAP_MS = 2000;
constexpr unsigned long TRANSFER_PROGRESS_GAP_MS = 4000;

// Copies a file name without its extension, truncated to the buffer.
template <size_t N>
void copyDisplayName(char (&dst)[N], const std::string& src) {
  size_t len = src.size();
  const size_t dot = src.find_last_of('.');
  if (dot != std::string::npos && dot > 0) len = dot;
  if (len > N - 1) len = N - 1;
  memcpy(dst, src.data(), len);
  dst[len] = '\0';
}

// "676 / 726 KB" or "1.2 / 3.4 MB" (both in the larger unit); just the received bytes when total is 0.
std::string formatProgress(const size_t received, const size_t total) {
  const size_t scale = total > 0 ? total : received;
  const bool mb = scale >= 1024 * 1024;
  const double unit = mb ? 1024.0 * 1024.0 : 1024.0;
  const char* fmt = mb ? "%.1f" : "%.0f";
  char a[16];
  char b[16];
  snprintf(a, sizeof(a), fmt, received / unit);
  if (total == 0) return std::string(a) + (mb ? " MB" : " KB");
  snprintf(b, sizeof(b), fmt, total / unit);
  return std::string(a) + " / " + b + (mb ? " MB" : " KB");
}

// Content-Length includes the multipart framing, so hold 99% until the file is stored.
int transferPercent(const size_t received, const size_t total) {
  if (total == 0) return -1;
  const auto percent = static_cast<int>(static_cast<uint64_t>(received) * 100 / total);
  return percent > 99 ? 99 : percent;
}

// DNS server for captive portal (redirects all DNS queries to our IP)
DNSServer* dnsServer = nullptr;
constexpr uint16_t DNS_PORT = 53;

void stopDnsServer() {
  if (!dnsServer) return;

  dnsServer->stop();
  delete dnsServer;
  dnsServer = nullptr;
}

void restartMdns(const char* hostname, const char* tag) {
  MDNS.end();
  if (MDNS.begin(hostname)) {
    LOG_DBG(tag, "mDNS started: http://%s.local/", hostname);
  } else {
    LOG_DBG(tag, "WARNING: mDNS failed to start");
  }
}

// 0..4 bars from RSSI (dBm), with 3 dBm hysteresis on currentBars to suppress flicker.
int barsForRssi(int rssi, int currentBars) {
  static constexpr int RISE_DBM[] = {-85, -75, -65, -55};
  static constexpr int FALL_DBM[] = {-88, -78, -68, -58};
  int bars = std::clamp(currentBars, 0, 4);
  while (bars < 4 && rssi >= RISE_DBM[bars]) bars++;
  while (bars > 0 && rssi < FALL_DBM[bars - 1]) bars--;
  return bars;
}
}  // namespace

void CrossPointWebServerActivity::onEnter() {
  Activity::onEnter();

  LOG_DBG("WEBACT", "Free heap at onEnter: %d bytes", ESP.getFreeHeap());

  // Heap-critical transition: WiFi (~45KB) plus the web server have to fit in
  // what's left of the ~380KB parts. SD-font caches retained for the CJK UI
  // fallback (mini glyph/kern arenas, ligature tables) are rebuildable on
  // demand — release them up front instead of aborting in startWebServer()
  // when the heap comes up short (observed on X3 with a Korean SD font).
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
    LOG_DBG("WEBACT", "Free heap after SD font cache release: %d bytes", ESP.getFreeHeap());
  }

  // Reset state
  state = WebServerActivityState::MODE_SELECTION;
  networkMode = NetworkMode::JOIN_NETWORK;
  isApMode = false;
  connectedIP.clear();
  connectedSSID.clear();
  lastHandleClientTime = 0;
  requestUpdate();

  if (startInJoinNetwork) {
    // Came back from the heap-defrag reboot on a pristine heap: skip the mode
    // picker and go straight into Join Network (onNetworkModeSelected won't
    // reboot again while this flag is set).
    onNetworkModeSelected(NetworkMode::JOIN_NETWORK);
    return;
  }

  // Launch network mode selection subactivity
  LOG_DBG("WEBACT", "Launching NetworkModeSelectionActivity...");
  startActivityForResult(std::make_unique<NetworkModeSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             onGoHome();
                           } else {
                             onNetworkModeSelected(std::get<NetworkModeResult>(result.data).mode);
                           }
                         });
}

void CrossPointWebServerActivity::onExit() {
  Activity::onExit();

  LOG_DBG("WEBACT", "Free heap at onExit start: %d bytes", ESP.getFreeHeap());

  state = WebServerActivityState::SHUTTING_DOWN;
  stopDnsServer();
  MDNS.end();

  // Web uploads may have installed or removed plugins. Non-touch devices
  // reboot below (setup() re-reads everything), but touch devices end the
  // session in place, so re-read event subscriptions here.
  pluginevents::refreshSubscriptions();

  // Skip reboot if WiFi was never activated (e.g. user backed out of mode selection).
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    if (isApMode) {
      WiFi.softAPdisconnect(true);
    } else {
      WiFi.disconnect(false);
    }
    delay(30);
    silentRestart();
  }

  LOG_DBG("WEBACT", "Free heap at onExit end: %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServerActivity::onNetworkModeSelected(const NetworkMode mode) {
  // Join Network brings up WiFi + the web server + TLS relays, whose working set
  // needs a large *contiguous* block. After reading/browsing the heap is
  // fragmented enough that tight boards (X3) abort mid-activation. Reboot once
  // into a pristine heap and land straight back here; the flag prevents a second
  // reboot on that return. No-op on touch boards, which fall through.
  if (mode == NetworkMode::JOIN_NETWORK && !startInJoinNetwork) {
    // Deck: loading segments in the Join card still on screen instead of the popup.
    const bool deckOverlay = deck::listsActive();
    if (deckOverlay) deck::drawJoinLoading(renderer);
    silentRestartToJoinNetwork(deckOverlay);  // does not return on non-touch boards
  }

  const char* modeName = "Join Network";
  if (mode == NetworkMode::CONNECT_CALIBRE) {
    modeName = "Connect to Calibre";
  } else if (mode == NetworkMode::CREATE_HOTSPOT) {
    modeName = "Create Hotspot";
#if FREEINK_CAP_USB_MSC
  } else if (mode == NetworkMode::USB_DRIVE) {
    modeName = "USB Drive";
#endif
  }
  LOG_DBG("WEBACT", "Network mode selected: %s", modeName);

#if FREEINK_CAP_USB_MSC
  if (mode == NetworkMode::USB_DRIVE) {
    activityManager.goToUsbDrive();
    return;
  }
#endif

  networkMode = mode;
  isApMode = (mode == NetworkMode::CREATE_HOTSPOT);

  if (mode == NetworkMode::CONNECT_CALIBRE) {
    startActivityForResult(
        std::make_unique<CalibreConnectActivity>(renderer, mappedInput), [this](const ActivityResult& result) {
          state = WebServerActivityState::MODE_SELECTION;

          startActivityForResult(std::make_unique<NetworkModeSelectionActivity>(renderer, mappedInput),
                                 [this](const ActivityResult& result) {
                                   if (result.isCancelled) {
                                     onGoHome();
                                   } else {
                                     onNetworkModeSelected(std::get<NetworkModeResult>(result.data).mode);
                                   }
                                 });
        });
    return;
  }

  if (mode == NetworkMode::JOIN_NETWORK) {
    // STA mode - launch WiFi selection
    LOG_DBG("WEBACT", "Turning on WiFi (STA mode)...");
    WiFi.mode(WIFI_STA);

    state = WebServerActivityState::WIFI_SELECTION;
    LOG_DBG("WEBACT", "Launching WifiSelectionActivity...");
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& wifi = std::get<WifiResult>(result.data);
                               connectedIP = wifi.ip;
                               connectedSSID = wifi.ssid;
                             }
                             onWifiSelectionComplete(!result.isCancelled);
                           });
  } else {
    // AP mode - start access point
    state = WebServerActivityState::AP_STARTING;
    requestUpdate();
    startAccessPoint();
  }
}

void CrossPointWebServerActivity::onWifiSelectionComplete(const bool connected) {
  LOG_DBG("WEBACT", "WifiSelectionActivity completed, connected=%d", connected);

  if (connected) {
    // Get connection info before exiting subactivity
    isApMode = false;

    // Start mDNS for hostname resolution
    restartMdns(AP_HOSTNAME, "WEBACT");

    // Start the web server
    startWebServer();
  } else {
    // User cancelled - go back to mode selection
    state = WebServerActivityState::MODE_SELECTION;

    startActivityForResult(std::make_unique<NetworkModeSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) {
                               onGoHome();
                             } else {
                               onNetworkModeSelected(std::get<NetworkModeResult>(result.data).mode);
                             }
                           });
  }
}

void CrossPointWebServerActivity::startAccessPoint() {
  LOG_DBG("WEBACT", "Starting Access Point mode...");
  LOG_DBG("WEBACT", "Free heap before AP start: %d bytes", ESP.getFreeHeap());

  // Configure and start the AP
  WiFi.mode(WIFI_AP);
  delay(100);

  // Start soft AP
  bool apStarted;
  if (AP_PASSWORD && strlen(AP_PASSWORD) >= 8) {
    apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
  } else {
    // Open network (no password)
    apStarted = WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
  }

  if (!apStarted) {
    LOG_ERR("WEBACT", "ERROR: Failed to start Access Point!");
    onGoHome();
    return;
  }

  delay(100);  // Wait for AP to fully initialize

  // Get AP IP address
  const IPAddress apIP = WiFi.softAPIP();
  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
  connectedIP = ipStr;
  connectedSSID = AP_SSID;

  LOG_DBG("WEBACT", "Access Point started!");
  LOG_DBG("WEBACT", "SSID: %s", AP_SSID);
  LOG_DBG("WEBACT", "IP: %s", connectedIP.c_str());

  // Start mDNS for hostname resolution
  restartMdns(AP_HOSTNAME, "WEBACT");

  // Start DNS server for captive portal behavior
  // This redirects all DNS queries to our IP, making any domain typed resolve to us
  stopDnsServer();
  dnsServer = new DNSServer();
  dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer->start(DNS_PORT, "*", apIP);
  LOG_DBG("WEBACT", "DNS server started for captive portal");

  LOG_DBG("WEBACT", "Free heap after AP start: %d bytes", ESP.getFreeHeap());

  // Start the web server
  startWebServer();
}

void CrossPointWebServerActivity::startWebServer() {
  LOG_DBG("WEBACT", "Starting web server...");

  // Repeat the release right before the allocation: the WiFi selection screen
  // rendered since onEnter(), and a CJK SSID repopulates the SD-font caches.
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("WEBACT", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("WEBACT", "Free heap before server alloc: %d bytes", ESP.getFreeHeap());
  }

  // Create the web server instance
  webServer.reset(new CrossPointWebServer());
  // An upload holds loop() inside handleClient(), so input is sampled on each received chunk instead. On a slow
  // link chunks come ~0.5 s apart, so a button found down gets a second sample past the SDK's 5 ms debounce.
  webServer->setUploadCancelCheck([this] {
    mappedInput.update(true);
    if (gpio.rawInputActive()) {
      delay(6);
      mappedInput.update(true);
    }
    leaveRequested = leaveRequested || mappedInput.isPressed(MappedInputManager::Button::Back) ||
                     mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture();
    return leaveRequested;
  });
  webServer->setUploadProgressHook([this] { pollTransfer(); });
  webServer->begin();

  if (webServer->isRunning()) {
    state = WebServerActivityState::SERVER_RUNNING;
    LOG_DBG("WEBACT", "Web server started successfully");
    lastWifiBars = isApMode ? 0 : barsForRssi(WiFi.RSSI(), 0);

    // The device is online: deliver queued plugin events through their
    // manifest handlers. STA only (AP mode has no internet route); bounded by
    // the drain's per-call event budget so serving is not noticeably delayed.
    if (!isApMode) {
      pluginevents::drain(&renderer);
    }

    // Force an immediate render since we're transitioning from a subactivity
    // that had its own rendering task. We need to make sure our display is shown.
    requestUpdate();
  } else {
    LOG_ERR("WEBACT", "ERROR: Failed to start web server!");
    webServer.reset();
    // Go back on error
    onGoHome();
  }
}

void CrossPointWebServerActivity::loop() {
  // Handle different states
  if (state == WebServerActivityState::SERVER_RUNNING) {
    // Handle DNS requests for captive portal (AP mode only)
    if (isApMode && dnsServer) {
      dnsServer->processNextRequest();
    }

    // STA mode: Monitor WiFi connection health
    if (!isApMode && webServer && webServer->isRunning()) {
      static unsigned long lastWifiCheck = 0;
      if (millis() - lastWifiCheck > 2000) {  // Check every 2 seconds
        lastWifiCheck = millis();
        const wl_status_t wifiStatus = WiFi.status();
        // Driver auto-reconnect handles retries; abandon (via onGoHome) only
        // after WIFI_ABANDON_MS, otherwise the activity freezes on a blip.
        bool repaint = false;
        if (wifiStatus != WL_CONNECTED) {
          if (consecutiveDisconnects == 0) {
            firstDisconnectAt = millis();
            repaint = true;
          }
          consecutiveDisconnects++;
          LOG_DBG("WEBACT", "WiFi not connected (status=%d, consecutive=%d, total=%lu ms)", wifiStatus,
                  consecutiveDisconnects, millis() - firstDisconnectAt);
          if (millis() - firstDisconnectAt > WIFI_ABANDON_MS) {
            LOG_DBG("WEBACT", "WiFi unavailable for >%lu s; returning to network selection", WIFI_ABANDON_MS / 1000UL);
            state = WebServerActivityState::SHUTTING_DOWN;
            onGoHome();
            return;
          }
        } else {
          if (consecutiveDisconnects > 0) {
            LOG_DBG("WEBACT", "WiFi recovered after %d failed checks (%lu ms)", consecutiveDisconnects,
                    millis() - firstDisconnectAt);
            repaint = true;
          }
          consecutiveDisconnects = 0;
          firstDisconnectAt = 0;
          const int rssi = WiFi.RSSI();
          if (rssi < -75) {
            LOG_DBG("WEBACT", "Warning: Weak WiFi signal: %d dBm", rssi);
          }
          const int bars = barsForRssi(rssi, lastWifiBars);
          if (bars != lastWifiBars) {
            lastWifiBars = bars;
            repaint = true;
          }
        }
        if (repaint) requestUpdate();
      }
    }

    pollTransfer();

    // Handle web server requests - maximize throughput with watchdog safety
    if (webServer && webServer->isRunning()) {
      const unsigned long timeSinceLastHandleClient = millis() - lastHandleClientTime;

      // Log if there's a significant gap between handleClient calls (>100ms)
      if (lastHandleClientTime > 0 && timeSinceLastHandleClient > 100) {
        LOG_DBG("WEBACT", "WARNING: %lu ms gap since last handleClient", timeSinceLastHandleClient);
      }

      // Reset watchdog BEFORE processing - HTTP header parsing can be slow
      resetTaskWatchdogIfSubscribed();

      // Process HTTP requests in tight loop for maximum throughput
      // More iterations = more data processed per main loop cycle
      constexpr int MAX_ITERATIONS = 500;
      for (int i = 0; i < MAX_ITERATIONS && webServer->isRunning(); i++) {
        webServer->handleClient();
        if (leaveRequested) {
          onGoHome();
          return;
        }
        // Reset watchdog every 32 iterations
        if ((i & 0x1F) == 0x1F) {
          resetTaskWatchdogIfSubscribed();
        }
        // Yield and check for exit button every 64 iterations
        if ((i & 0x3F) == 0x3F) {
          yield();
          // Pump input inside this blocking loop so exit events remain responsive.
          mappedInput.update(true);
          // Home remains available now; other configured actions are deferred
          // to the next main-loop pass.
          if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) {
            onGoHome();
            return;
          }
        }
      }
      lastHandleClientTime = millis();
    }

    // Also check outside the request-processing loop.
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) {
      onGoHome();
      return;
    }
  }
}

void CrossPointWebServerActivity::pollTransfer() {
  if (!webServer || !webServer->isRunning() || leaveRequested) return;
  const unsigned long now = millis();
  if (now - lastTransferPoll < TRANSFER_POLL_MS) return;
  lastTransferPoll = now;

  const auto status = webServer->getUploadStatus();
  char name[sizeof(transfer.name)] = "";
  if (status.inProgress) copyDisplayName(name, status.filename);

  // Only this task writes `transfer`, so reading it here needs no lock.
  const bool changed = status.completedCount != transfer.count || status.inProgress != transfer.active ||
                       (status.inProgress && strcmp(name, transfer.name) != 0);
  const bool progressed = status.inProgress && status.received != transfer.received;
  if (changed || progressed) {
    const bool store = changed || now - lastTransferDraw >= TRANSFER_PROGRESS_GAP_MS;
    if (store) {
      std::lock_guard<std::mutex> guard(transferMutex);
      transfer.active = status.inProgress;
      transfer.received = status.received;
      transfer.total = status.total;
      memcpy(transfer.name, name, sizeof(name));
      if (status.completedCount != transfer.count) {
        transfer.count = status.completedCount;
        copyDisplayName(transfer.lastName, status.lastCompleteName);
      }
      transferDrawPending = true;
    }
  }
  if (!transferDrawPending) return;
  if (now - lastTransferDraw < TRANSFER_MIN_GAP_MS) return;
  transferDrawPending = false;
  lastTransferDraw = now;
  // Immediate: during an HTTP upload this runs inside handleClient(), and a
  // deferred request would wait until the whole body has been read.
  requestUpdate(true);
}

CrossPointWebServerActivity::TransferView CrossPointWebServerActivity::transferSnapshot() const {
  std::lock_guard<std::mutex> guard(transferMutex);
  return transfer;
}

void CrossPointWebServerActivity::renderDeck(const TransferView& tv) const {
  // Deck v2 "Online": waiting (she reads in the window), or receiving / received (she catches books).
  const bool received = tv.active || tv.count > 0;
  const std::string host = isApMode ? std::string(AP_HOSTNAME) + ".local" : connectedIP;
  const std::string mode = isApMode ? tr(STR_HOTSPOT_MODE) : connectedSSID;
  std::string msg;
  std::string detail;
  int percent = -1;
  if (tv.active) {
    msg = std::string("RECEIVING \xc2\xb7 ") + tv.name;
    percent = transferPercent(tv.received, tv.total);
    detail = formatProgress(tv.received, tv.total);
  } else if (tv.count > 0) {
    msg = std::string("RECEIVED \xc2\xb7 ") + tv.lastName;
  } else {
    msg = "WAITING FOR FILES...";
  }
  if (tv.count > 0) {
    if (!detail.empty()) detail += '\n';
    detail += std::to_string(tv.count) + (tv.count == 1 ? " FILE IN" : " FILES IN");
  }
  deck::drawOnlineScreen(renderer, received, mode.c_str(), host.c_str(), msg.c_str(), tv.active, percent,
                         detail.c_str());
  const auto labels = mappedInput.mapLabels("STOP", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void CrossPointWebServerActivity::render(RenderLock&&) {
  const TransferView tv = transferSnapshot();
  if (deck::listsActive() && state == WebServerActivityState::SERVER_RUNNING) {
    renderDeck(tv);
    renderer.displayBuffer();
    return;
  }
  // Only render our own UI when server is running
  // Subactivities handle their own rendering
  if (state == WebServerActivityState::SERVER_RUNNING || state == WebServerActivityState::AP_STARTING) {
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();

    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   isApMode ? tr(STR_HOTSPOT_MODE) : tr(STR_FILE_TRANSFER), nullptr);

    if (state == WebServerActivityState::SERVER_RUNNING) {
      GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                        connectedSSID.c_str());
      renderServerRunning(tv);
    } else {
      const auto height = renderer.getLineHeight(UI_10_FONT_ID);
      const auto top = (pageHeight - height) / 2;
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_STARTING_HOTSPOT));
    }
    renderer.displayBuffer();
  }
}

void CrossPointWebServerActivity::renderServerRunning(const TransferView& tv) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 isApMode ? tr(STR_HOTSPOT_MODE) : tr(STR_FILE_TRANSFER), nullptr);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    connectedSSID.c_str());

  if (!isApMode) {
    renderWifiIndicator(metrics.topPadding + metrics.headerHeight);
  }

  int startY = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing * 2;
  int height10 = renderer.getLineHeight(UI_10_FONT_ID);
  if (isApMode) {
    // AP mode display
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, startY, tr(STR_CONNECT_WIFI_HINT), true,
                      EpdFontFamily::BOLD);
    startY += height10 + metrics.verticalSpacing * 2;

    // Show QR code for Wifi
    // follows spec at https://github.com/zxing/zxing/wiki/Barcode-Contents#wi-fi-network-config-android-ios-11
    const std::string wifiConfig = std::string("WIFI:T:nopass;S:") + connectedSSID + ";;";
    const Rect qrBoundsWifi(metrics.contentSidePadding, startY, QR_CODE_WIDTH, QR_CODE_HEIGHT);
    QrUtils::drawQrCode(renderer, qrBoundsWifi, wifiConfig);

    // Show network name
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding + QR_CODE_WIDTH + metrics.verticalSpacing, startY + 80,
                      connectedSSID.c_str());

    startY += QR_CODE_HEIGHT + 2 * metrics.verticalSpacing;

    // Show primary URL (hostname)
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, startY, tr(STR_OPEN_URL_HINT), true,
                      EpdFontFamily::BOLD);
    startY += height10 + metrics.verticalSpacing * 2;

    std::string hostnameUrl = std::string("http://") + AP_HOSTNAME + ".local/";
    std::string ipUrl = tr(STR_OR_HTTP_PREFIX) + connectedIP + "/";

    // Show QR code for URL
    const Rect qrBoundsUrl(metrics.contentSidePadding, startY, QR_CODE_WIDTH, QR_CODE_HEIGHT);
    QrUtils::drawQrCode(renderer, qrBoundsUrl, hostnameUrl);

    // Show IP address as fallback
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding + QR_CODE_WIDTH + metrics.verticalSpacing, startY + 80,
                      hostnameUrl.c_str());
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding + QR_CODE_WIDTH + metrics.verticalSpacing, startY + 100,
                      ipUrl.c_str());
  } else {
    startY += metrics.verticalSpacing * 2;

    // STA mode display (original behavior)
    // std::string ipInfo = "IP Address: " + connectedIP;
    renderer.drawCenteredText(UI_10_FONT_ID, startY, tr(STR_OPEN_URL_HINT), true, EpdFontFamily::BOLD);
    startY += height10;
    renderer.drawCenteredText(UI_10_FONT_ID, startY, tr(STR_SCAN_QR_HINT), true, EpdFontFamily::BOLD);
    startY += height10 + metrics.verticalSpacing * 2;

    // Show QR code for URL
    std::string webInfo = "http://" + connectedIP + "/";
    const Rect qrBounds((pageWidth - QR_CODE_WIDTH) / 2, startY, QR_CODE_WIDTH, QR_CODE_HEIGHT);
    QrUtils::drawQrCode(renderer, qrBounds, webInfo);
    startY += QR_CODE_HEIGHT + metrics.verticalSpacing * 2;

    // Show web server URL prominently
    renderer.drawCenteredText(UI_10_FONT_ID, startY, webInfo.c_str(), true);
    startY += height10 + 5;

    // Also show hostname URL
    std::string hostnameUrl = std::string(tr(STR_OR_HTTP_PREFIX)) + AP_HOSTNAME + ".local/";
    renderer.drawCenteredText(SMALL_FONT_ID, startY, hostnameUrl.c_str(), true);
  }

  renderTransferStatus(tv);

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// Current upload (name, bar, bytes) and the last file received, above the button hints.
void CrossPointWebServerActivity::renderTransferStatus(const TransferView& tv) const {
  if (!tv.active && tv.count == 0) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int x = metrics.contentSidePadding;
  const int width = pageWidth - 2 * metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int percent = tv.active ? transferPercent(tv.received, tv.total) : -1;

  int blockHeight = 0;
  if (tv.active) blockHeight += lineHeight + metrics.verticalSpacing;
  if (percent >= 0) blockHeight += metrics.progressBarHeight + metrics.verticalSpacing;
  if (tv.active) blockHeight += lineHeight + metrics.verticalSpacing;
  if (tv.count > 0) blockHeight += lineHeight;
  int y = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing * 2 - blockHeight;
  // Keep the block clear of the content above it.
  renderer.fillRect(0, y - metrics.verticalSpacing, pageWidth, blockHeight + metrics.verticalSpacing, false);

  if (tv.active) {
    const std::string label = std::string(tr(STR_TRANSFER_RECEIVING)) + tv.name;
    renderer.drawText(SMALL_FONT_ID, x, y,
                      renderer.truncatedText(SMALL_FONT_ID, label.c_str(), width, EpdFontFamily::BOLD).c_str(), true,
                      EpdFontFamily::BOLD);
    y += lineHeight + metrics.verticalSpacing;
    if (percent >= 0) {
      const int fill = (width - 4) * percent / 100;
      renderer.drawRect(x, y, width, metrics.progressBarHeight);
      if (fill > 0) renderer.fillRect(x + 2, y + 2, fill, metrics.progressBarHeight - 4);
      y += metrics.progressBarHeight + metrics.verticalSpacing;
    }
    std::string bytes = formatProgress(tv.received, tv.total);
    if (percent >= 0) bytes += "  (" + std::to_string(percent) + "%)";
    renderer.drawText(SMALL_FONT_ID, x, y, bytes.c_str());
    y += lineHeight + metrics.verticalSpacing;
  }
  if (tv.count > 0) {
    char count[48];
    snprintf(count, sizeof(count), tr(STR_FILES_RECEIVED), static_cast<unsigned>(tv.count));
    const std::string line = std::string(count) + " \xc2\xb7 " + tv.lastName;
    renderer.drawText(SMALL_FONT_ID, x, y, renderer.truncatedText(SMALL_FONT_ID, line.c_str(), width).c_str());
  }
}

void CrossPointWebServerActivity::renderWifiIndicator(int subHeaderTop) const {
  constexpr int BAR_COUNT = 4;
  constexpr int BAR_WIDTH = 4;
  constexpr int BAR_GAP = 2;
  constexpr int ICON_HEIGHT = 14;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int iconWidth = BAR_COUNT * BAR_WIDTH + (BAR_COUNT - 1) * BAR_GAP;
  const int iconRight = renderer.getScreenWidth() - metrics.contentSidePadding;
  const int iconLeft = iconRight - iconWidth;
  const int iconBottom = subHeaderTop + metrics.tabBarHeight - metrics.verticalSpacing;

  const bool wifiUp = (WiFi.status() == WL_CONNECTED) && (consecutiveDisconnects == 0);
  if (wifiUp) {
    for (int i = 0; i < BAR_COUNT; i++) {
      const int barHeight = (i + 1) * ICON_HEIGHT / BAR_COUNT;
      const int x = iconLeft + i * (BAR_WIDTH + BAR_GAP);
      const int y = iconBottom - barHeight;
      if (i < lastWifiBars) {
        renderer.fillRect(x, y, BAR_WIDTH, barHeight, true);
      } else {
        renderer.drawRect(x, y, BAR_WIDTH, barHeight, true);
      }
    }
  } else {
    const int xSize = ICON_HEIGHT;
    const int x0 = iconRight - xSize;
    const int y0 = iconBottom - xSize;
    renderer.drawLine(x0, y0, x0 + xSize, y0 + xSize, 2, true);
    renderer.drawLine(x0, y0 + xSize, x0 + xSize, y0, 2, true);
  }
}
