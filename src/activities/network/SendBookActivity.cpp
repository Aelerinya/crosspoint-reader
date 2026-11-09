#include "SendBookActivity.h"

#include <Bookplate.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <TrustedTime.h>
#include <Txt.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/RenderLock.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/Sprite.h"
#include "components/UITheme.h"
#include "components/themes/extra/DeckBookplate.h"
#include "components/themes/extra/DeckList.h"
#include "components/themes/extra/DeckScreens.h"
#include "components/themes/extra/DeckTheme.h"
#include "fontIds.h"
#include "images/DeckOperator.h"
#include "util/TaskWatchdog.h"

namespace {

constexpr unsigned long CONNECT_TIMEOUT_MS = 15000;
constexpr size_t CHUNK_SIZE = 4096;
// The picker popup does not scroll; this many rows fit on the X4 in every theme.
constexpr size_t MAX_NETWORKS = 8;
// Deck book card thumbs: the Library's row height, and the Sending screen's large cover.
constexpr int COVER_SMALL_HEIGHT = deck::kBookThumbHeight;
constexpr int COVER_LARGE_HEIGHT = 110;
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

class FileInput final : public BookplateInput {
  HalFile& file;
  uint32_t length;

 public:
  explicit FileInput(HalFile& f) : file(f), length(static_cast<uint32_t>(f.fileSize())) {}
  uint32_t size() const override { return length; }
  bool readAt(const uint32_t offset, uint8_t* buf, const size_t len) override {
    if (!file.seek(offset)) return false;
    size_t done = 0;
    while (done < len) {
      const int n = file.read(buf + done, len - done);
      if (n <= 0) return false;
      done += static_cast<size_t>(n);
    }
    return true;
  }
};

// Bookplate memory. Before reading the book: enough free heap to try at all
// (the X4C's PSRAM; the X4 with Wi-Fi up never has it). After reading the zip
// index, BookplateEpub reports what the rewrite needs; drawing the image adds the
// 48 KB page copy, the ~49 KB stored-deflate stream and the PNG built from it.
// Short of any of these, the book goes out unchanged.
constexpr size_t PLATE_MIN_FREE = 256 * 1024;
constexpr size_t PLATE_PNG_SIZE = 49 * 1024;
constexpr size_t PLATE_IMAGE_WORK = 48 * 1024 + 2 * PLATE_PNG_SIZE;
constexpr size_t PLATE_MARGIN = 64 * 1024;

const char* const MONTHS[] = {"January", "February", "March",     "April",   "May",      "June",
                              "July",    "August",   "September", "October", "November", "December"};

bool endsWithEpub(const std::string& name) {
  if (name.size() < 5) return false;
  std::string ext = name.substr(name.size() - 5);
  for (char& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return ext == ".epub";
}

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

SendBookActivity::SendBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                                   std::string title, const bool fromReader)
    : Activity("SendBook", renderer, mappedInput),
      bookPath(std::move(path)),
      bookTitle(std::move(title)),
      fromReader(fromReader) {
  const size_t slash = bookPath.rfind('/');
  bookName = slash == std::string::npos ? bookPath : bookPath.substr(slash + 1);
}

void SendBookActivity::onEnter() {
  Activity::onEnter();
  if (deck::listsActive()) {
    const size_t dot = bookName.rfind('.');
    std::string meta = dot == std::string::npos ? std::string() : bookName.substr(dot + 1);
    HalFile file;
    if (Storage.openFileForRead("SEND", bookPath, file)) {
      if (!meta.empty()) meta += " \xc2\xb7 ";
      meta += deck::formatBookSize(static_cast<uint32_t>(file.fileSize()));
      file.close();
    }
    RenderLock lock(*this);
    if (bookTitle.empty()) bookTitle = dot == std::string::npos ? bookName : bookName.substr(0, dot);
    bookMeta = std::move(meta);
  }
  WiFi.persistent(false);  // never store the peer's hotspot as a saved network
  startScan();
}

void SendBookActivity::onExit() {
  Activity::onExit();
  WiFi.scanDelete();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    // Same heap cleanup as the other Wi-Fi screens.
    if (fromReader) {
      silentRestartToReader();
    } else {
      silentRestart();
    }
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
  sentBytes = 0;
  bytesSent = false;
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
  if (deck::listsActive()) {
    {
      RenderLock lock(*this);
      pickIndex = 0;
      state = State::Picking;
    }
    requestUpdate();
    return;
  }
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
  {
    RenderLock lock(*this);  // the Deck screens draw peer.ssid
    peer = networks[index];
    peerFirmware = PeerFirmware::Unknown;
  }
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

bool SendBookActivity::preparePlate(BookplateInput& input, BookplateEpub& plated) {
  if (!SETTINGS.bookplateEnabled || SETTINGS.bookplateName[0] == '\0' || !endsWithEpub(bookName)) return false;
  if (HalMemory::getDefaultHeap().freeBytes < PLATE_MIN_FREE) {
    LOG_INF("SEND", "No bookplate: low memory");
    return false;
  }
  std::string error;
  if (!plated.open(input, &error)) {
    LOG_ERR("SEND", "No bookplate: %s", error.c_str());
    return false;
  }
  const bool image = SETTINGS.bookplateStyle == CrossPointSettings::BOOKPLATE_IMAGE_TEXT;
  const size_t pngSize = image ? PLATE_PNG_SIZE : 0;
  const size_t need = plated.memoryNeeded(pngSize) + (image ? PLATE_IMAGE_WORK : 0) + PLATE_MARGIN;
  const size_t block = std::max(plated.largestAllocation(pngSize), image ? PLATE_PNG_SIZE : size_t{0});
  const HalMemory::HeapStats heap = HalMemory::getDefaultHeap();
  if (heap.freeBytes < need || heap.largestBlockBytes < block) {
    LOG_INF("SEND", "No bookplate: needs %u (block %u), %u free (block %u)", static_cast<unsigned>(need),
            static_cast<unsigned>(block), static_cast<unsigned>(heap.freeBytes),
            static_cast<unsigned>(heap.largestBlockBytes));
    return false;
  }

  BookplateContent content;
  content.title = plated.title();
  if (content.title.empty()) content.title = bookName.substr(0, bookName.size() - 5);
  content.from = SETTINGS.bookplateName;
  std::string shortDate;
  if (const int64_t now = trustedtime::trustedNow()) {
    const time_t t = static_cast<time_t>(now);
    tm local{};
    localtime_r(&t, &local);
    char buf[40];
    snprintf(buf, sizeof(buf), "%d %s %d", local.tm_mday, MONTHS[local.tm_mon], local.tm_year + 1900);
    content.date = buf;
    snprintf(buf, sizeof(buf), "%02d %.3s %d", local.tm_mday, MONTHS[local.tm_mon], local.tm_year + 1900);
    shortDate = buf;
    for (char& c : shortDate) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }
  if (image) {
    // Drawn in the framebuffer; the Sending screen repaints it right after.
    RenderLock lock(*this);
    content.png = deck::renderBookplatePng(renderer, content.title, content.from, shortDate);
    if (content.png.empty()) LOG_ERR("SEND", "Bookplate image failed; text only");
  }
  if (!plated.prepare(content, &error)) {
    LOG_ERR("SEND", "No bookplate: %s", error.c_str());
    return false;
  }
  LOG_DBG("SEND", "Bookplate added: %u -> %u bytes", static_cast<unsigned>(input.size()),
          static_cast<unsigned>(plated.size()));
  return true;
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
  FileInput input(file);
  BookplateEpub plated;
  const bool withPlate = preparePlate(input, plated);
  if (!withPlate) file.seek(0);
  request.size = withPlate ? plated.size() : static_cast<uint32_t>(file.fileSize());
  request.overwrite = overwrite;

  sentBytes = 0;
  bytesSent = false;
  totalBytes = request.size;
  cancelRequested = false;
  {
    RenderLock lock(*this);
    state = State::Sending;
  }
  requestUpdateAndWait();

  FileSource fileSource(file);
  BookSendSource& source = withPlate ? static_cast<BookSendSource&>(plated) : fileSource;
  const BookSendResult result = BookSender::send(
      transport, source, request, chunk.get(), CHUNK_SIZE,
      [](void* ctx, const uint32_t sent, const uint32_t total) {
        auto* self = static_cast<SendBookActivity*>(ctx);
        resetTaskWatchdogIfSubscribed();
        if (sent > 0) self->bytesSent = true;
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
      if (deck::listsActive()) {
        {
          RenderLock lock(*this);
          replaceChoice = 1;
          state = State::AskReplace;
        }
        requestUpdate();
        return;
      }
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

void SendBookActivity::makeCoverThumb() {
  // One thumb per loop pass while the scan runs; the Library's 74 px one is often cached.
  const int height = coverStep == 0 ? COVER_SMALL_HEIGHT : COVER_LARGE_HEIGHT;
  coverStep++;
  if (!FsHelpers::hasReflowableBookExtension(bookName) || Txt::isTxtOrMd(bookName)) {
    coverStep = 2;
    return;
  }
  Epub epub(bookPath, "/.crosspoint");
  if (!epub.generateThumbBmpFromSource(height)) {
    coverStep = 2;  // no usable cover: skip the large one too
    return;
  }
  {
    RenderLock lock(*this);
    (height == COVER_SMALL_HEIGHT ? coverSmall : coverLarge) = epub.getThumbBmpPath(height);
  }
  if (height == COVER_SMALL_HEIGHT) requestUpdate();
}

// The reader list and the "already there" choice; true when the press was handled.
bool SendBookActivity::deckLoop() {
  const bool up = mappedInput.wasPressed(MappedInputManager::Button::NavPrevious);
  const bool down = mappedInput.wasPressed(MappedInputManager::Button::NavNext);
  const bool confirm = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool back = mappedInput.wasPressed(MappedInputManager::Button::Back);
  if (state == State::Picking) {
    const int count = static_cast<int>(networks.size());
    if (back) {
      finish();
    } else if (confirm && count > 0) {
      choose(pickIndex);
    } else if ((up || down) && count > 0) {
      {
        RenderLock lock(*this);
        pickIndex = (pickIndex + (down ? 1 : count - 1)) % count;
      }
      requestUpdate();
    } else if (coverStep < 2) {
      makeCoverThumb();  // the scan beat the thumbs; finish them between presses
    }
    return true;
  }
  if (state == State::AskReplace) {
    if (back) {
      finish();
    } else if (confirm) {
      if (replaceChoice == 1) {
        transfer(true);
      } else {
        finish();
      }
    } else if (up || down) {
      {
        RenderLock lock(*this);
        replaceChoice = 1 - replaceChoice;
      }
      requestUpdate();
    }
    return true;
  }
  return false;
}

void SendBookActivity::loop() {
  holdAwakeOnce = false;

  if (deck::listsActive()) {
    if (deckLoop()) return;
    if (state == State::Scanning && coverStep < 2) makeCoverThumb();
  }

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
  if (deck::listsActive()) {
    renderDeck();
    return;
  }
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

void SendBookActivity::renderDeck() {
  const GfxRenderer& r = renderer;
  const int W = r.getScreenWidth();
  const int small = SMALL_FONT_ID;
  const bool art = deck::companionOn();
  const deck::BookCardInfo book{bookTitle, bookMeta, coverSmall};
  const std::string ssid = deck::upper(peer.ssid);
  const char* firmware = peerFirmware == PeerFirmware::Stock                         ? tr(STR_SEND_STOCK_READER)
                         : peerFirmware == PeerFirmware::CrossPoint                  ? tr(STR_SEND_CROSSPOINT_READER)
                         : peer.known && peer.known->firmware == PeerFirmware::Stock ? tr(STR_SEND_STOCK_READER)
                         : peer.known                                                ? tr(STR_SEND_CROSSPOINT_READER)
                                                                                     : nullptr;
  const std::string target = firmware ? std::string(firmware) + " \xc2\xb7 " + peer.ssid : peer.ssid;
  const char* backLabel = tr(STR_BACK);
  const char* confirmLabel = "";
  const char* upLabel = "";
  const char* downLabel = "";

  renderer.clearScreen();
  switch (state) {
    case State::Scanning:
    case State::EnteringPassword:
    case State::Connecting: {
      const bool scanning = state != State::Connecting;
      deck::drawSmallHeader(r, "SEND");
      deck::drawSendBookCard(r, book);
      deck::drawStatePanel(r, scanning ? "SEARCHING" : "CONNECTING", nullptr, 151);
      int artTop = 300;
      if (scanning) {
        deck::drawTracked(r, small, 16, 276, "LOOKING FOR READERS NEARBY", 2, true, true);
      } else {
        deck::drawArrowTag(r, 16, 276, target.c_str());
        artTop = 318;
      }
      if (art) drawSprite(r, kOpScanM, (W - kOpScanM.width) / 2 + 4, artTop);
      // No real progress to show: a fixed partial bar, fuller once connecting.
      deck::segments(r, 22, 668, W - 44, 14, 20, scanning ? 6 : 12);
      backLabel = tr(STR_CANCEL);
      break;
    }
    case State::NoneFound: {
      deck::drawSmallHeader(r, "SEND");
      deck::drawSendBookCard(r, book);
      deck::drawStatePanel(r, "NO READER", nullptr, 151);
      deck::drawTracked(r, small, 16, 278, "ON THE OTHER READER, OPEN:", 2, true, true);
      const int cardW = 292;
      const struct {
        const char* kicker;
        const char* route;
      } cards[] = {{"XTEINK \xc2\xb7 STOCK", "Upgrade / Transfer > Phone transfer"},
                   {"CROSSPOINT", "File transfer > Create hotspot"}};
      int y = 300;
      for (const auto& card : cards) {
        // Measure first: the card grows with the wrapped route.
        const int textTop = y + 34;
        const int bottom = deck::drawRouteText(r, 30, textTop, cardW - 36, card.route);
        const int h = bottom - y + 16;
        r.fillRect(16, y, cardW, h, false);
        deck::drawChamfer(r, 16, y, cardW, h, 12, 2);
        deck::drawTracked(r, small, 30, y + 12, card.kicker, 2, true, true);
        deck::drawRouteText(r, 30, textTop, cardW - 36, card.route);
        y += h + 12;
      }
      if (art) drawSprite(r, kOpLens, W - 12 - kOpLens.width, 304);
      confirmLabel = tr(STR_RETRY);
      break;
    }
    case State::Picking: {
      char found[24];
      snprintf(found, sizeof(found), "%u FOUND", static_cast<unsigned>(networks.size()));
      deck::drawSmallHeader(r, "SEND", found);
      deck::drawSendBookCard(r, book);
      deck::drawBigTitle(r, 152, "READERS NEARBY", nullptr, 31);
      constexpr int kRowH = 66;
      const int top = 207;
      const int listBottom = r.getScreenHeight() - 54 - 10;
      const int count = static_cast<int>(networks.size());
      const int fit = std::max(1, (listBottom - top) / kRowH);
      const int first = pickIndex >= fit ? pickIndex - fit + 1 : 0;
      const int name = deck::displayFont(13);
      const int cap = deck::bigTitleCap(13);
      int y = top;
      for (int i = first; i < count && y + kRowH <= listBottom; ++i, y += kRowH) {
        const Network& net = networks[i];
        const bool on = i == pickIndex;
        const bool ink = !on;
        const int cy = y + kRowH / 2;
        if (on) {
          r.fillRect(16, y, W - 32, kRowH, true);
        } else {
          for (int x = 16; x < W - 16; x += 6) r.fillRect(x, y + kRowH - 1, 3, 1, true);
        }
        deck::drawReaderGlyph(r, 28, cy - 12, 17, 24, ink);
        int right = W - 28;
        if (on) {
          const int xs[] = {right - 10, right, right - 10};
          const int ys[] = {cy - 7, cy, cy + 7};
          r.fillPolygon(xs, ys, 3, false);
          right -= 22;
        }
        const int level = net.rssi >= -55 ? 4 : net.rssi >= -65 ? 3 : net.rssi >= -75 ? 2 : 1;
        deck::drawSignalBars(r, right - 28, cy - 7, level, ink);
        right -= 44;
        const char* how;
        if (net.known && net.known->firmware == PeerFirmware::Stock) {
          deck::drawOutlineTag(r, right, cy - 6, "XTEINK", ink);
          right -= deck::trackedWidth(r, small, "XTEINK", 1, true) + 24;
          how = "STOCK FIRMWARE \xc2\xb7 PHONE TRANSFER";
        } else if (net.known) {
          deck::drawOutlineTag(r, right, cy - 6, "CROSSPOINT", ink);
          right -= deck::trackedWidth(r, small, "CROSSPOINT", 1, true) + 24;
          how = "CROSSPOINT \xc2\xb7 HOTSPOT";
        } else {
          how = net.encrypted ? "WI-FI \xc2\xb7 PASSWORD" : "WI-FI \xc2\xb7 OPEN";
        }
        const std::string label = r.truncatedText(name, deck::upper(net.ssid).c_str(), right - 60);
        const int nameTop = y + 15;
        r.drawText(name, 60, nameTop - deck::displayCapOffset(r, 13), label.c_str(), ink);
        const std::string line = r.truncatedText(small, how, W - 28 - 60, EpdFontFamily::BOLD);
        deck::drawTracked(r, small, 60, nameTop + cap + 7, line.c_str(), 2, ink, true);
      }
      if (art && listBottom - kOpScanS.height > y + 10) {
        drawSprite(r, kOpScanS, W - 20 - kOpScanS.width, listBottom - kOpScanS.height);
      }
      backLabel = tr(STR_CANCEL);
      confirmLabel = "SEND";
      upLabel = tr(STR_DIR_UP);
      downLabel = tr(STR_DIR_DOWN);
      break;
    }
    case State::AskReplace: {
      deck::drawSmallHeader(r, "SEND");
      deck::drawSendBookCard(r, book);
      deck::hatchShade(r);
      const int x0 = 22, y0 = 202, w = W - 44, h = 368;
      deck::fillChamfer(r, x0, y0, w, h, 24, true);
      deck::fillChamfer(r, x0 + 3, y0 + 3, w - 6, h - 6, 23, false);
      {
        // Reader name tag flush right, slanted left edge.
        const std::string t = r.truncatedText(UI_10_FONT_ID, ssid.c_str(), w - 80, EpdFontFamily::BOLD);
        const int tw = deck::trackedWidth(r, UI_10_FONT_ID, t.c_str(), 2, true);
        const int th = r.getLineHeight(UI_10_FONT_ID) + 14;
        const int tx = x0 + w - 3 - tw - 30;
        const int ty = y0 + 19;
        const int xs[] = {tx, x0 + w - 3, x0 + w - 3, tx - 10};
        const int ys[] = {ty, ty, ty + th, ty + th};
        r.fillPolygon(xs, ys, 4, true);
        deck::drawTracked(r, UI_10_FONT_ID, tx + 14, ty + 7, t.c_str(), 2, false, true);
      }
      r.drawText(deck::displayFont(31), x0 + 26, y0 + 70 - deck::displayCapOffset(r, 31), "ALREADY THERE");
      const std::string line =
          r.truncatedText(small, "THE OTHER READER HAS THIS BOOK. REPLACE IT?", w - 52, EpdFontFamily::BOLD);
      deck::drawTracked(r, small, x0 + 26, y0 + 132, line.c_str(), 1, true, true);
      const char* options[2] = {"KEEP THEIRS", "REPLACE IT"};
      for (int i = 0; i < 2; ++i) {
        const int oy = y0 + 192 + i * 66;
        const int ow = w - 52, oh = 54;
        const bool on = i == replaceChoice;
        if (on) {
          deck::fillChamfer(r, x0 + 25, oy, ow, oh, 14, true);
          const int cy = oy + oh / 2;
          const int xs[] = {x0 + 44, x0 + 56, x0 + 44};
          const int ys[] = {cy - 7, cy, cy + 7};
          r.fillPolygon(xs, ys, 3, false);
        } else {
          deck::drawChamfer(r, x0 + 25, oy, ow, oh, 14, 2);
        }
        const int f = deck::displayFont(22);
        r.drawText(f, x0 + 66, oy + (oh - deck::bigTitleCap(22)) / 2 - deck::displayCapOffset(r, 22), options[i], !on);
      }
      backLabel = tr(STR_CANCEL);
      confirmLabel = tr(STR_SELECT);
      upLabel = tr(STR_DIR_UP);
      downLabel = tr(STR_DIR_DOWN);
      break;
    }
    case State::Sending: {
      deck::drawSmallHeader(r, "SEND");
      deck::drawStatePanel(r, "SENDING", nullptr, 53);
      // Cover ▸ segments ▸ the other reader.
      deck::drawBookCover(r, coverLarge.empty() ? coverSmall : coverLarge, 16, 183, 74, 110);
      const uint32_t total = totalBytes;
      const uint32_t sent = sentBytes;
      const int percent = total ? static_cast<int>(static_cast<uint64_t>(sent) * 100 / total) : 0;
      const int segX = 104, segW = 370 - segX;
      deck::segments(r, segX, 232, segW, 12, 12, percent * 12 / 100);
      {
        const int xs[] = {376, 386, 376};
        const int ys[] = {232, 238, 244};
        r.fillPolygon(xs, ys, 3, true);
      }
      deck::drawReaderGlyph(r, W - 16 - 56, 188, 56, 100);
      const std::string id = r.truncatedText(small, ssid.c_str(), 160, EpdFontFamily::BOLD);
      const int idW = deck::trackedWidth(r, small, id.c_str(), 2, true);
      const std::string title =
          r.truncatedText(small, deck::upper(bookTitle).c_str(), W - 32 - idW - 24, EpdFontFamily::BOLD);
      deck::drawTracked(r, small, 16, 304, title.c_str(), 2, true, true);
      deck::drawTracked(r, small, W - 16 - idW, 304, id.c_str(), 2, true, true);
      if (art) drawSprite(r, kOpThrow, (W - kOpThrow.width) / 2 + 8, 330);
      char pct[8];
      snprintf(pct, sizeof(pct), "%d%%", percent);
      r.drawText(deck::displayFont(31), 16, 676 - deck::displayCapOffset(r, 31), pct);
      char sizes[40];
      snprintf(sizes, sizeof(sizes), "%.1f / %.1f MB", static_cast<double>(sent) / (1024.0 * 1024.0),
               static_cast<double>(total) / (1024.0 * 1024.0));
      const int sw = deck::trackedWidth(r, small, sizes, 2, true);
      deck::drawTracked(r, small, W - 16 - sw, 692, sizes, 2, true, true);
      deck::segments(r, 16, 722, W - 32, 14, 20, percent * 20 / 100);
      backLabel = tr(STR_CANCEL);
      break;
    }
    case State::Done: {
      deck::drawSmallHeader(r, "SEND");
      deck::drawStatePanel(r, "SENT", nullptr, 53);
      deck::drawArrowTag(r, 16, 177, ("ON " + target).c_str());
      if (art) drawSprite(r, kOpTrophy, (W - kOpTrophy.width) / 2, 226);
      r.fillRect(16, 650, W - 32, 2, true);
      deck::drawBookCover(r, coverSmall, 16, 660, 50, 74);
      const std::string size = deck::formatBookSize(totalBytes);
      const int sw = deck::trackedWidth(r, small, size.c_str(), 2, true);
      deck::drawTracked(r, small, W - 16 - sw, 690, size.c_str(), 2, true, true);
      const int f = deck::displayFont(13);
      const std::string title = r.truncatedText(f, deck::upper(bookTitle).c_str(), W - 16 - sw - 20 - 80);
      r.drawText(f, 80, 688 - deck::displayCapOffset(r, 13), title.c_str());
      confirmLabel = tr(STR_DONE);
      break;
    }
    case State::Failed: {
      deck::drawSmallHeader(r, "SEND");
      deck::drawStatePanel(r, "FAILED", nullptr, 53);
      const std::string line = r.truncatedText(small, deck::upper(peer.ssid + " \xc2\xb7 " + failureDetail).c_str(),
                                               W - 40, EpdFontFamily::BOLD);
      deck::drawTracked(r, small, 16, 179, line.c_str(), 2, true, true);
      if (art) drawSprite(r, kOpFail, (W - kOpFail.width) / 2, 216);
      // Only true while no byte went out: a cut upload can leave a partial file behind.
      if (!bytesSent) deck::drawTracked(r, small, 16, 678, "NOTHING WAS CHANGED ON THE OTHER READER", 2, true, true);
      confirmLabel = tr(STR_RETRY);
      break;
    }
  }
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, upLabel, downLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
