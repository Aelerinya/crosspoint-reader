#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Sends a book to another reader's web server. See README.md for the two
// receiver protocols (stock Xteink and CrossPoint).

enum class PeerFirmware : uint8_t { Unknown, Stock, CrossPoint };

// A receiver's transfer hotspot, matched by SSID during the Wi-Fi scan.
struct PeerHotspot {
  const char* ssid;
  const char* password;  // nullptr for an open network
  PeerFirmware firmware;
  const char* fallbackHost;  // used when the gateway address is unknown
};

const PeerHotspot* findPeerHotspot(const char* ssid);

class BookSendTransport {
 public:
  virtual ~BookSendTransport() = default;
  virtual bool connect(const char* host, uint16_t port) = 0;
  // Writes all bytes or fails.
  virtual bool write(const uint8_t* data, size_t len) = 0;
  // Returns bytes read, 0 when nothing arrived within timeoutMs, -1 once the peer closed.
  virtual int read(uint8_t* buf, size_t len, uint32_t timeoutMs) = 0;
  virtual void close() = 0;
};

class BookSendSource {
 public:
  virtual ~BookSendSource() = default;
  // Returns bytes read, 0 at end of file, -1 on error.
  virtual int read(uint8_t* buf, size_t len) = 0;
};

enum class BookSendError : uint8_t {
  Ok,
  ConnectFailed,
  WriteFailed,
  NoResponse,
  SourceReadFailed,
  AlreadyExists,  // CrossPoint refused to replace an existing file
  Rejected,       // any other non-2xx answer; see BookSendResult::message
  Cancelled,
  UnknownPeer,
};

struct BookSendRequest {
  const char* host = nullptr;
  uint16_t port = 80;
  PeerFirmware firmware = PeerFirmware::Unknown;
  const char* fileName = nullptr;  // bare name, no folder
  const char* destDir = "/Books";  // created on the receiver if missing
  uint32_t size = 0;
  bool overwrite = false;  // CrossPoint only; stock has no such option
};

struct BookSendResult {
  BookSendError error = BookSendError::Ok;
  int httpStatus = 0;
  std::string message;  // start of the receiver's response body, for display
};

class BookSender {
 public:
  using ProgressFn = void (*)(void* ctx, uint32_t sent, uint32_t total);

  // Probes /api/status (CrossPoint: 200 with its JSON status), then /status
  // (stock: any 200). Each probe opens its own connection.
  static PeerFirmware detect(BookSendTransport& transport, const char* host, uint16_t port = 80);

  // Makes destDir on the receiver, then streams `size` bytes from source.
  // `buffer` is the chunk buffer for reading the source (4 KB is plenty).
  // `cancel`, when set, is polled between chunks.
  static BookSendResult send(BookSendTransport& transport, BookSendSource& source, const BookSendRequest& request,
                             uint8_t* buffer, size_t bufferSize, ProgressFn progress = nullptr,
                             void* progressCtx = nullptr, const volatile bool* cancel = nullptr);

  // Exposed for tests.
  static std::string urlEncode(const std::string& value);
  static std::string headerSafeFileName(const std::string& name);
  static int parseStatusLine(const char* line);
};
