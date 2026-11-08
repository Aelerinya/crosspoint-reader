#include "CalibreConnectActivity.h"

#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "components/themes/extra/DeckList.h"
#include "components/themes/extra/DeckScreens.h"
#include "components/themes/extra/DeckTheme.h"
#include "fontIds.h"
#include "images/DeckOperator.h"
#include "util/TaskWatchdog.h"

namespace {
constexpr const char* HOSTNAME = "crosspoint";
}  // namespace

void CalibreConnectActivity::onEnter() {
  Activity::onEnter();

  requestUpdate();
  state = CalibreConnectState::WIFI_SELECTION;
  connectedIP.clear();
  connectedSSID.clear();
  lastHandleClientTime = 0;
  lastProgressReceived = 0;
  lastProgressTotal = 0;
  currentUploadName.clear();
  lastCompleteName.clear();
  lastCompleteAt = 0;
  lastProcessedCompleteAt = 0;
  exitRequested = false;

  if (WiFi.status() != WL_CONNECTED) {
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
    connectedIP = WiFi.localIP().toString().c_str();
    connectedSSID = WiFi.SSID().c_str();
    startWebServer();
  }
}

void CalibreConnectActivity::onExit() {
  Activity::onExit();

  MDNS.end();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void CalibreConnectActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    finish();
    return;
  }

  startWebServer();
}

void CalibreConnectActivity::startWebServer() {
  state = CalibreConnectState::SERVER_STARTING;
  requestUpdate();

  MDNS.end();
  if (MDNS.begin(HOSTNAME)) {
    // mDNS is optional for the Calibre plugin but still helpful for users.
    LOG_DBG("CAL", "mDNS started: http://%s.local/", HOSTNAME);
  }

  // Heap-critical allocation: SD-font caches retained for the CJK UI fallback
  // are rebuildable — release them (again: the WiFi selection screen may have
  // repopulated them rendering a CJK SSID) so the server object doesn't abort
  // on OOM. See CrossPointWebServerActivity::startWebServer().
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("CAL", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("CAL", "Free heap before server alloc: %d bytes", ESP.getFreeHeap());
  }

  webServer.reset(new CrossPointWebServer());
  webServer->begin();

  if (webServer->isRunning()) {
    state = CalibreConnectState::SERVER_RUNNING;
    requestUpdate();
  } else {
    state = CalibreConnectState::ERROR;
    requestUpdate();
  }
}

void CalibreConnectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    exitRequested = true;
  }

  if (webServer && webServer->isRunning()) {
    const unsigned long timeSinceLastHandleClient = millis() - lastHandleClientTime;
    if (lastHandleClientTime > 0 && timeSinceLastHandleClient > 100) {
      LOG_DBG("CAL", "WARNING: %lu ms gap since last handleClient", timeSinceLastHandleClient);
    }

    resetTaskWatchdogIfSubscribed();
    constexpr int MAX_ITERATIONS = 80;
    for (int i = 0; i < MAX_ITERATIONS && webServer->isRunning(); i++) {
      webServer->handleClient();
      if ((i & 0x07) == 0x07) {
        resetTaskWatchdogIfSubscribed();
      }
      if ((i & 0x0F) == 0x0F) {
        yield();
        if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
          exitRequested = true;
          break;
        }
      }
    }
    lastHandleClientTime = millis();

    const auto status = webServer->getWsUploadStatus();
    bool changed = false;
    if (status.inProgress) {
      if (status.received != lastProgressReceived || status.total != lastProgressTotal ||
          status.filename != currentUploadName) {
        lastProgressReceived = status.received;
        lastProgressTotal = status.total;
        currentUploadName = status.filename;
        changed = true;
      }
    } else if (lastProgressReceived != 0 || lastProgressTotal != 0) {
      lastProgressReceived = 0;
      lastProgressTotal = 0;
      currentUploadName.clear();
      changed = true;
    }
    // Only update lastCompleteAt if the server has a NEW value (not one we already processed)
    // This prevents restoring an old value after the 6s timeout clears it
    if (status.lastCompleteAt != 0 && status.lastCompleteAt != lastProcessedCompleteAt) {
      lastCompleteAt = status.lastCompleteAt;
      lastCompleteName = status.lastCompleteName;
      lastProcessedCompleteAt = status.lastCompleteAt;  // Mark this value as processed
      changed = true;
    }
    if (lastCompleteAt > 0 && (millis() - lastCompleteAt) >= 6000) {
      lastCompleteAt = 0;
      lastCompleteName.clear();
      // Note: we DON'T reset lastProcessedCompleteAt here, so we won't re-process the old server value
      changed = true;
    }
    if (changed) {
      requestUpdate();
    }
  }

  if (exitRequested) {
    finish();
    return;
  }
}

namespace {
// "1) Install ..." / "\"Keep ...\"" -> the bare step text.
std::string calibreStep(const char* raw) {
  std::string t = raw ? raw : "";
  const size_t paren = t.find(") ");
  if (paren != std::string::npos && paren < 3) t.erase(0, paren + 2);
  std::string out;
  for (const char c : t) {
    if (c != '"') out += c;
  }
  return out;
}
}  // namespace

void CalibreConnectActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  if (deck::listsActive()) {
    const GfxRenderer& r = renderer;
    const int small = SMALL_FONT_ID;
    deck::drawSmallHeader(r, "CALIBRE");
    if (state == CalibreConnectState::ERROR) {
      deck::drawStatePanel(r, "FAILED");
      deck::drawTracked(r, small, 16, 187, deck::upper(tr(STR_CONNECTION_FAILED)).c_str(), 2, true, true);
      if (deck::companionOn()) drawSprite(r, kOpFail, (pageWidth - kOpFail.width) / 2, 236);
    } else {
      deck::drawStatePanel(r, "CALIBRE", "待機");
      std::string status =
          state == CalibreConnectState::SERVER_RUNNING ? "WAITING FOR CALIBRE..." : tr(STR_CALIBRE_STARTING);
      const bool receiving = lastProgressTotal > 0 && lastProgressReceived <= lastProgressTotal;
      if (receiving) {
        status = std::string(tr(STR_CALIBRE_RECEIVING)) + currentUploadName;
      } else if (lastCompleteAt > 0 && (millis() - lastCompleteAt) < 6000) {
        status = std::string(tr(STR_CALIBRE_RECEIVED)) + lastCompleteName;
      }
      deck::drawBlockTag(r, 16, 186, status.c_str());
      if (state == CalibreConnectState::SERVER_RUNNING) {
        r.drawRect(16, 228, pageWidth - 32, 84, 3, true);
        const std::string net = deck::upper(connectedSSID + " \xc2\xb7 IP");
        deck::drawTracked(r, small, 33, 242, r.truncatedText(small, net.c_str(), pageWidth - 70).c_str(), 2, true,
                          true);
        r.drawText(UI_12_FONT_ID, 33, 266, connectedIP.c_str(), true, EpdFontFamily::BOLD);
        int y = 330;
        if (receiving) {
          deck::segments(r, 16, y, pageWidth - 32, 14, 20,
                         static_cast<int>(20ull * lastProgressReceived / std::max<size_t>(1, lastProgressTotal)));
          y += 30;
        }
        const char* steps[] = {tr(STR_CALIBRE_INSTRUCTION_1), tr(STR_CALIBRE_INSTRUCTION_2),
                               tr(STR_CALIBRE_INSTRUCTION_3), tr(STR_CALIBRE_INSTRUCTION_4)};
        const int num = deck::displayFont(22);
        for (int i = 0; i < 4; ++i) {
          char n[4];
          snprintf(n, sizeof(n), "%d", i + 1);
          r.drawText(num, 16, y + 2 - deck::displayCapOffset(r, 22), n);
          const auto lines = deck::wrapDisplay(r, small, calibreStep(steps[i]), 230, 2);
          int ly = y;
          for (const auto& line : lines) {
            deck::drawTracked(r, small, 60, ly, line.c_str(), 2, true, true);
            ly += r.getLineHeight(small) + 2;
          }
          y = std::max(ly, y + 32) + 12;
        }
      }
      if (deck::companionOn()) {
        drawSprite(r, kOpNet, pageWidth - 12 - kOpNet.width, pageHeight - 54 - 4 - kOpNet.height);
      }
    }
    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CALIBRE_WIRELESS));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  if (state == CalibreConnectState::SERVER_STARTING) {
    renderer.drawCenteredText(UI_12_FONT_ID, top, tr(STR_CALIBRE_STARTING));
  } else if (state == CalibreConnectState::ERROR) {
    renderer.drawCenteredText(UI_12_FONT_ID, top, tr(STR_CONNECTION_FAILED), true, EpdFontFamily::BOLD);
  } else if (state == CalibreConnectState::SERVER_RUNNING) {
    GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                      connectedSSID.c_str(), (std::string(tr(STR_IP_ADDRESS_PREFIX)) + connectedIP).c_str());

    int y = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing * 4;
    const auto heightText12 = renderer.getTextHeight(UI_12_FONT_ID);
    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, y, tr(STR_CALIBRE_SETUP), true, EpdFontFamily::BOLD);
    y += heightText12 + metrics.verticalSpacing * 2;

    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y, tr(STR_CALIBRE_INSTRUCTION_1));
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y + height, tr(STR_CALIBRE_INSTRUCTION_2));
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y + height * 2, tr(STR_CALIBRE_INSTRUCTION_3));
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y + height * 3, tr(STR_CALIBRE_INSTRUCTION_4));

    y += height * 3 + metrics.verticalSpacing * 4;
    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, y, tr(STR_CALIBRE_STATUS), true, EpdFontFamily::BOLD);
    y += heightText12 + metrics.verticalSpacing * 2;

    if (lastProgressTotal > 0 && lastProgressReceived <= lastProgressTotal) {
      std::string label = tr(STR_CALIBRE_RECEIVING);
      if (!currentUploadName.empty()) {
        label += ": " + currentUploadName;
        label = renderer.truncatedText(SMALL_FONT_ID, label.c_str(), pageWidth - metrics.contentSidePadding * 2,
                                       EpdFontFamily::REGULAR);
      }
      renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y, label.c_str());
      GUI.drawProgressBar(renderer,
                          Rect{metrics.contentSidePadding, y + height + metrics.verticalSpacing,
                               pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
                          lastProgressReceived, lastProgressTotal);
      y += height + metrics.verticalSpacing * 2 + metrics.progressBarHeight;
    }

    if (lastCompleteAt > 0 && (millis() - lastCompleteAt) < 6000) {
      std::string msg = std::string(tr(STR_CALIBRE_RECEIVED)) + lastCompleteName;
      msg = renderer.truncatedText(SMALL_FONT_ID, msg.c_str(), pageWidth - metrics.contentSidePadding * 2,
                                   EpdFontFamily::REGULAR);
      renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y, msg.c_str());
    }

    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
