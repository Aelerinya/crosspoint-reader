#include "SendBookActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/RenderLock.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TaskWatchdog.h"

namespace {

constexpr unsigned long CONNECT_TIMEOUT_MS = 15000;
constexpr size_t CHUNK_SIZE = 4096;
// The picker popup does not scroll; this many rows fit on the X4 in every theme.
constexpr size_t MAX_NETWORKS = 8;
// Used when DHCP gave no gateway on a network that is not a default reader hotspot.
constexpr const char* DEFAULT_PEER_HOST = "192.168.4.1";

class WifiTransport final : public BookSendTransport {
  WiFiClient client;

 public:
  bool connect(const char* host, const uint16_t port) override { return client.connect(host, port) == 1; }
  bool write(const uint8_t* data, const size_t len) override { return client.write(data, len) == len; }
  int read(uint8_t* buf, const size_t len, const uint32_t timeoutMs) override {
    const unsigned long start = millis();
    while (true) {
      const int avail = client.available();
      if (avail > 0) return client.read(buf, len < static_cast<size_t>(avail) ? len : static_cast<size_t>(avail));
      if (!client.connected()) return -1;
      if (millis() - start >= timeoutMs) return 0;
      resetTaskWatchdogIfSubscribed();
      delay(5);
    }
  }
  void close() override { client.stop(); }
};

class FileSource final : public BookSendSource {
  HalFile& file;

 public:
  explicit FileSource(HalFile& f) : file(f) {}
  int read(uint8_t* buf, const size_t len) override { return file.read(buf, len); }
};

const char* errorText(const BookSendError error) {
  switch (error) {
    case BookSendError::ConnectFailed:
      return tr(STR_SEND_ERR_CONNECT);
    case BookSendError::WriteFailed:
      return tr(STR_SEND_ERR_LOST);
    case BookSendError::NoResponse:
      return tr(STR_SEND_ERR_NO_ANSWER);
    case BookSendError::SourceReadFailed:
      return tr(STR_SEND_ERR_READ);
    default:
      return tr(STR_SEND_ERR_UNKNOWN);
  }
}

}  // namespace

SendBookActivity::SendBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("SendBook", renderer, mappedInput), bookPath(std::move(path)) {
  const size_t slash = bookPath.rfind('/');
  bookName = slash == std::string::npos ? bookPath : bookPath.substr(slash + 1);
}

void SendBookActivity::onEnter() {
  Activity::onEnter();
  WiFi.persistent(false);  // never store the peer's hotspot as a saved network
  startScan();
}

void SendBookActivity::onExit() {
  Activity::onExit();
  WiFi.scanDelete();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();  // same heap cleanup as the other Wi-Fi screens
  }
}

const char* SendBookActivity::peerLabel() const {
  const PeerFirmware firmware = peerFirmware != PeerFirmware::Unknown ? peerFirmware
                                : peer.known                          ? peer.known->firmware
                                                                      : PeerFirmware::Unknown;
  if (firmware == PeerFirmware::Stock) return tr(STR_SEND_STOCK_READER);
  if (firmware == PeerFirmware::CrossPoint) return tr(STR_SEND_CROSSPOINT_READER);
  return peer.ssid.c_str();
}

void SendBookActivity::setState(const State next) {
  {
    RenderLock lock(*this);
    state = next;
  }
  requestUpdate();
}

void SendBookActivity::startScan() {
  setState(State::Scanning);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.scanNetworks(true);
}

void SendBookActivity::collectScanResults(const int count) {
  networks.clear();
  networks.reserve(std::min<size_t>(count, MAX_NETWORKS * 2));
  for (int i = 0; i < count; i++) {
    std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty()) continue;  // hidden network
    const int32_t rssi = WiFi.RSSI(i);
    const bool encrypted = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    auto it = std::find_if(networks.begin(), networks.end(), [&](const Network& n) { return n.ssid == ssid; });
    if (it == networks.end()) {
      const PeerHotspot* known = findPeerHotspot(ssid.c_str());
      networks.push_back(Network{std::move(ssid), rssi, encrypted, known});
    } else if (rssi > it->rssi) {
      it->rssi = rssi;
      it->encrypted = encrypted;
    }
  }
  WiFi.scanDelete();

  // Default reader hotspots first, then by signal strength.
  std::stable_sort(networks.begin(), networks.end(), [](const Network& a, const Network& b) {
    if ((a.known != nullptr) != (b.known != nullptr)) return a.known != nullptr;
    return a.rssi > b.rssi;
  });
  if (networks.size() > MAX_NETWORKS) networks.resize(MAX_NETWORKS);

  if (networks.empty()) {
    setState(State::NoneFound);
  } else {
    showPicker();
  }
}

void SendBookActivity::showPicker() {
  std::vector<std::string> labels;
  labels.reserve(networks.size());
  for (const Network& n : networks) {
    if (!n.known) {
      labels.push_back(n.ssid);
    } else {
      labels.emplace_back(n.known->firmware == PeerFirmware::Stock ? tr(STR_SEND_STOCK_READER)
                                                                   : tr(STR_SEND_CROSSPOINT_READER));
    }
  }
  {
    RenderLock lock(*this);
    picker.show(StrId::STR_SEND_PICK_NETWORK, labels, 0, [this](const int index) { choose(index); });
    state = State::Picking;
  }
  requestUpdate();
}

void SendBookActivity::choose(const int index) {
  peer = networks[index];
  peerFirmware = PeerFirmware::Unknown;
  const bool knownPassword = peer.known && (peer.known->password || !peer.encrypted);
  if (knownPassword || !peer.encrypted) {
    connectTo(peer.known ? peer.known->password : nullptr);
    return;
  }
  {
    RenderLock lock(*this);
    state = State::EnteringPassword;
  }
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ENTER_WIFI_PASSWORD), "",
                                                                 64, InputType::Text),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             showPicker();
                           } else {
                             connectTo(std::get<KeyboardResult>(result.data).text.c_str());
                           }
                         });
}

void SendBookActivity::connectTo(const char* password) {
  connectStartedAt = millis();
  LOG_DBG("SEND", "Joining %s", peer.ssid.c_str());
  WiFi.begin(peer.ssid.c_str(), password);
  setState(State::Connecting);
}

void SendBookActivity::fail(const std::string& detail) {
  LOG_ERR("SEND", "Failed: %s", detail.c_str());
  {
    RenderLock lock(*this);
    failureDetail = detail;
    state = State::Failed;
  }
  requestUpdate();
}

void SendBookActivity::transfer(const bool overwrite) {
  const IPAddress gateway = WiFi.gatewayIP();
  const std::string host = gateway != IPAddress() ? gateway.toString().c_str()
                           : peer.known           ? peer.known->fallbackHost
                                                  : DEFAULT_PEER_HOST;

  // The probe decides the protocol. A default hotspot name is the fallback for a
  // receiver that does not answer the probe, as before. The receiver must be
  // the access point: on a shared network the gateway is the router.
  WifiTransport transport;
  if (peerFirmware == PeerFirmware::Unknown) {
    const PeerFirmware probed = BookSender::detect(transport, host.c_str());
    LOG_DBG("SEND", "%s at %s probed as firmware %d", peer.ssid.c_str(), host.c_str(), static_cast<int>(probed));
    if (peer.known) {
      peerFirmware = probed != PeerFirmware::Unknown ? probed : peer.known->firmware;
    } else {
      // Stock firmware cannot rename its hotspot, and a bare 200 on /status
      // is too weak a sign on someone's router.
      peerFirmware = probed == PeerFirmware::CrossPoint ? probed : PeerFirmware::Unknown;
    }
  }
  if (peerFirmware == PeerFirmware::Unknown) {
    WiFi.disconnect();
    fail(tr(STR_SEND_ERR_NOT_READER));
    return;
  }

  HalFile file;
  if (!Storage.openFileForRead("SEND", bookPath, file)) {
    fail(errorText(BookSendError::SourceReadFailed));
    return;
  }
  auto chunk = makeUniqueNoThrow<uint8_t[]>(CHUNK_SIZE);
  if (!chunk) {
    LOG_ERR("SEND", "OOM: %u byte chunk buffer", static_cast<unsigned>(CHUNK_SIZE));
    fail(tr(STR_MEMORY_ERROR));
    return;
  }

  BookSendRequest request;
  request.host = host.c_str();
  request.firmware = peerFirmware;
  request.fileName = bookName.c_str();
  request.size = static_cast<uint32_t>(file.fileSize());
  request.overwrite = overwrite;

  sentBytes = 0;
  totalBytes = request.size;
  cancelRequested = false;
  {
    RenderLock lock(*this);
    state = State::Sending;
  }
  requestUpdateAndWait();

  FileSource source(file);
  const BookSendResult result = BookSender::send(
      transport, source, request, chunk.get(), CHUNK_SIZE,
      [](void* ctx, const uint32_t sent, const uint32_t total) {
        auto* self = static_cast<SendBookActivity*>(ctx);
        resetTaskWatchdogIfSubscribed();
        self->mappedInput.update(true);
        if (self->mappedInput.wasPressed(MappedInputManager::Button::Back)) self->cancelRequested = true;
        // Repaint only every 10%: each e-ink refresh is slow next to a 4 KB chunk. Immediate,
        // because a deferred request is only delivered after this blocking loop() returns.
        const uint32_t step = total / 10 + 1;
        if (sent == total || sent / step != self->sentBytes / step) {
          self->sentBytes = sent;
          self->requestUpdate(true);
        }
      },
      this, &cancelRequested);
  file.close();
  // The main loop's inactivity timer did not run during the transfer.
  holdAwakeOnce = true;

  LOG_DBG("SEND", "Result %d, HTTP %d: %s", static_cast<int>(result.error), result.httpStatus, result.message.c_str());
  switch (result.error) {
    case BookSendError::Ok:
      setState(State::Done);
      return;
    case BookSendError::Cancelled:
      finish();
      return;
    case BookSendError::AlreadyExists: {
      // A popup rather than a ConfirmationActivity, so holdAwakeOnce still covers this frame.
      {
        RenderLock lock(*this);
        const char* options[] = {tr(STR_CANCEL), tr(STR_CONFIRM)};
        picker.showMessage("", tr(STR_SEND_EXISTS), options, 2, 0, [this](const int index) {
          if (index == 1) {
            transfer(true);
          } else {
            finish();
          }
        });
        state = State::AskReplace;
      }
      requestUpdate();
      return;
    }
    case BookSendError::Rejected:
      fail(result.message.empty() ? "HTTP " + std::to_string(result.httpStatus) : result.message);
      return;
    default:
      fail(errorText(result.error));
      return;
  }
}

void SendBookActivity::loop() {
  holdAwakeOnce = false;

  if (state == State::Picking || state == State::AskReplace) {
    const State asked = state;
    if (picker.handleInput(mappedInput, [this] { requestUpdate(); })) return;
    if (state == asked) finish();  // dismissed without a choice
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  switch (state) {
    case State::Scanning: {
      const int count = WiFi.scanComplete();
      if (count == WIFI_SCAN_RUNNING) return;
      collectScanResults(count < 0 ? 0 : count);
      return;
    }
    case State::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        transfer(false);
      } else if (WiFi.status() == WL_CONNECT_FAILED || WiFi.status() == WL_NO_SSID_AVAIL) {
        WiFi.disconnect();
        fail(tr(STR_CONNECTION_FAILED));
      } else if (millis() - connectStartedAt > CONNECT_TIMEOUT_MS) {
        WiFi.disconnect();
        fail(tr(STR_ERROR_CONNECTION_TIMEOUT));
      }
      return;
    case State::NoneFound:
    case State::Failed:
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) startScan();
      return;
    case State::Done:
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) finish();
      return;
    default:
      return;
  }
}

void SendBookActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int textWidth = pageWidth - metrics.contentSidePadding * 2;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  int y = pageHeight / 2 - lineHeight * 2;

  renderer.clearScreen();
  const std::string label = renderer.truncatedText(UI_10_FONT_ID, peerLabel(), textWidth);
  const std::string subtitle = renderer.truncatedText(SMALL_FONT_ID, bookName.c_str(), textWidth);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SEND_TO_READER),
                 subtitle.c_str());

  const char* confirmLabel = "";
  switch (state) {
    case State::Scanning:
    case State::Picking:
    case State::EnteringPassword:
      renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_SEND_SEARCHING));
      break;
    case State::NoneFound:
      renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_SEND_NONE_FOUND), true, EpdFontFamily::BOLD);
      y += lineHeight * 2;
      renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_SEND_HOW_TITLE));
      renderer.drawCenteredText(UI_10_FONT_ID, y + lineHeight * 3 / 2, tr(STR_SEND_HOW_STOCK));
      renderer.drawCenteredText(UI_10_FONT_ID, y + lineHeight * 3, tr(STR_SEND_HOW_CROSSPOINT));
      confirmLabel = tr(STR_RETRY);
      break;
    case State::AskReplace:
      renderer.drawCenteredText(UI_10_FONT_ID, y, label.c_str());
      break;
    case State::Connecting:
      renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_CONNECTING));
      renderer.drawCenteredText(UI_10_FONT_ID, y + lineHeight * 2, label.c_str());
      break;
    case State::Sending: {
      renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_SEND_SENDING));
      renderer.drawCenteredText(UI_10_FONT_ID, y + lineHeight * 3 / 2, label.c_str());
      GUI.drawProgressBar(renderer,
                          Rect{metrics.contentSidePadding, y + lineHeight * 3, textWidth, metrics.progressBarHeight},
                          sentBytes, totalBytes);
      char sizes[48];
      snprintf(sizes, sizeof(sizes), "%u / %u KB", static_cast<unsigned>(sentBytes / 1024),
               static_cast<unsigned>(totalBytes / 1024));
      renderer.drawCenteredText(SMALL_FONT_ID, y + lineHeight * 4 + metrics.progressBarHeight, sizes);
      break;
    }
    case State::Done:
      renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_SEND_DONE), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, y + lineHeight * 2, label.c_str());
      confirmLabel = tr(STR_DONE);
      break;
    case State::Failed: {
      renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_SEND_FAILED), true, EpdFontFamily::BOLD);
      const std::string detail = renderer.truncatedText(UI_10_FONT_ID, failureDetail.c_str(), textWidth);
      renderer.drawCenteredText(UI_10_FONT_ID, y + lineHeight * 2, detail.c_str());
      confirmLabel = tr(STR_RETRY);
      break;
    }
  }

  if (picker.processRender(renderer, mappedInput)) return;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
