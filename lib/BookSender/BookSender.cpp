#include "BookSender.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr PeerHotspot PEER_HOTSPOTS[] = {
    {"E-Paper", "12345678", PeerFirmware::Stock, "192.168.3.3"},
    {"CrossPoint-Reader", nullptr, PeerFirmware::CrossPoint, "192.168.4.1"},
};

constexpr const char* BOUNDARY = "----CrossPointBookSender7d3f";
constexpr uint32_t PROBE_TIMEOUT_MS = 4000;
// The receiver flushes to SD and clears caches before answering the upload.
constexpr uint32_t RESPONSE_TIMEOUT_MS = 20000;
constexpr size_t MAX_MESSAGE_LEN = 160;

struct HttpResponse {
  int status = 0;
  std::string body;
};

bool writeString(BookSendTransport& t, const std::string& s) {
  return t.write(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// Reads status line, headers and the start of the body until the peer closes.
// Bounded: keeps at most MAX_MESSAGE_LEN body bytes and a 512-byte header window.
bool readResponse(BookSendTransport& t, uint32_t timeoutMs, HttpResponse& out) {
  std::string head;
  bool inBody = false;
  uint8_t buf[128];
  while (true) {
    const int n = t.read(buf, sizeof(buf), timeoutMs);
    if (n < 0) break;
    if (n == 0) {
      if (out.status != 0) break;  // got an answer; the peer just kept the socket open
      return false;
    }
    for (int i = 0; i < n; i++) {
      const char c = static_cast<char>(buf[i]);
      if (inBody) {
        if (out.body.size() < MAX_MESSAGE_LEN) out.body += c;
        continue;
      }
      if (head.size() < 512) head += c;
      if (out.status == 0) {
        const size_t eol = head.find("\r\n");
        if (eol != std::string::npos) out.status = BookSender::parseStatusLine(head.substr(0, eol).c_str());
      }
      if (head.size() >= 4 && head.compare(head.size() - 4, 4, "\r\n\r\n") == 0) inBody = true;
    }
  }
  return out.status != 0;
}

bool request(BookSendTransport& t, const char* host, uint16_t port, const std::string& head, const std::string& body,
             uint32_t timeoutMs, HttpResponse& out) {
  if (!t.connect(host, port)) return false;
  const bool ok = writeString(t, head) && (body.empty() || writeString(t, body)) && readResponse(t, timeoutMs, out);
  t.close();
  return ok;
}

std::string requestHead(const char* method, const std::string& target, const char* host, const char* contentType,
                        size_t contentLength) {
  std::string h = std::string(method) + " " + target + " HTTP/1.1\r\nHost: " + host +
                  "\r\nUser-Agent: CrossPoint-BookSender\r\nConnection: close\r\n";
  if (contentType) {
    h += "Content-Type: ";
    h += contentType;
    h += "\r\nContent-Length: " + std::to_string(contentLength) + "\r\n";
  }
  return h + "\r\n";
}

std::string multipartContentType() { return std::string("multipart/form-data; boundary=") + BOUNDARY; }

std::string textPart(const char* name, const std::string& value) {
  return std::string("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + value +
         "\r\n";
}

std::string closingBoundary() { return std::string("--") + BOUNDARY + "--\r\n"; }

std::string joinPath(const std::string& dir, const std::string& name) {
  if (dir.empty() || dir == "/") return "/" + name;
  return dir.back() == '/' ? dir + name : dir + "/" + name;
}

// Best effort: an existing folder makes both receivers answer with an error we ignore.
void makeFolder(BookSendTransport& t, const BookSendRequest& r) {
  std::string dir = r.destDir ? r.destDir : "/";
  while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
  if (dir == "/") return;
  HttpResponse resp;
  if (r.firmware == PeerFirmware::Stock) {
    const std::string body = textPart("path", dir + "/") + closingBoundary();
    request(t, r.host, r.port, requestHead("PUT", "/edit", r.host, multipartContentType().c_str(), body.size()), body,
            PROBE_TIMEOUT_MS, resp);
  } else {
    const size_t slash = dir.rfind('/');
    const std::string parent = slash == 0 ? "/" : dir.substr(0, slash);
    const std::string body =
        "name=" + BookSender::urlEncode(dir.substr(slash + 1)) + "&path=" + BookSender::urlEncode(parent);
    request(t, r.host, r.port, requestHead("POST", "/mkdir", r.host, "application/x-www-form-urlencoded", body.size()),
            body, PROBE_TIMEOUT_MS, resp);
  }
}

BookSendResult fail(BookSendError error, int status = 0, std::string message = {}) {
  BookSendResult r;
  r.error = error;
  r.httpStatus = status;
  r.message = std::move(message);
  return r;
}

}  // namespace

const PeerHotspot* findPeerHotspot(const char* ssid) {
  if (!ssid) return nullptr;
  for (const auto& hotspot : PEER_HOTSPOTS) {
    if (strcmp(hotspot.ssid, ssid) == 0) return &hotspot;
  }
  return nullptr;
}

int BookSender::parseStatusLine(const char* line) {
  if (!line || strncmp(line, "HTTP/", 5) != 0) return 0;
  const char* space = strchr(line, ' ');
  if (!space) return 0;
  const int status = atoi(space + 1);
  return (status >= 100 && status <= 599) ? status : 0;
}

std::string BookSender::urlEncode(const std::string& value) {
  static constexpr char HEX[] = "0123456789ABCDEF";
  std::string out;
  for (const unsigned char c : value) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~' || c == '/') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += HEX[c >> 4];
      out += HEX[c & 0x0F];
    }
  }
  return out;
}

std::string BookSender::headerSafeFileName(const std::string& name) {
  std::string out = name;
  for (char& c : out) {
    if (c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = '_';
  }
  return out;
}

PeerFirmware BookSender::detect(BookSendTransport& transport, const char* host, const uint16_t port) {
  HttpResponse resp;
  if (request(transport, host, port, requestHead("GET", "/api/status", host, nullptr, 0), {}, PROBE_TIMEOUT_MS, resp) &&
      resp.status == 200) {
    return PeerFirmware::CrossPoint;
  }
  resp = {};
  if (request(transport, host, port, requestHead("GET", "/status", host, nullptr, 0), {}, PROBE_TIMEOUT_MS, resp) &&
      resp.status == 200) {
    return PeerFirmware::Stock;
  }
  return PeerFirmware::Unknown;
}

BookSendResult BookSender::send(BookSendTransport& transport, BookSendSource& source, const BookSendRequest& r,
                                uint8_t* buffer, const size_t bufferSize, const ProgressFn progress, void* progressCtx,
                                const volatile bool* cancel) {
  if (r.firmware == PeerFirmware::Unknown || !r.host || !r.fileName || !buffer || bufferSize == 0) {
    return fail(BookSendError::UnknownPeer);
  }

  makeFolder(transport, r);
  if (cancel && *cancel) return fail(BookSendError::Cancelled);

  const std::string dir = r.destDir ? r.destDir : "/";
  std::string target;
  std::string partHead;
  const std::string safeName = headerSafeFileName(r.fileName);
  if (r.firmware == PeerFirmware::Stock) {
    target = "/edit";
    partHead = std::string("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"data\"; filename=\"" +
               joinPath(dir, safeName) + "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
  } else {
    target = "/upload?path=" + urlEncode(dir) + (r.overwrite ? "&overwrite=1" : "");
    partHead = std::string("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" +
               safeName + "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
  }
  const std::string partTail = "\r\n" + closingBoundary();
  const size_t contentLength = partHead.size() + r.size + partTail.size();

  if (!transport.connect(r.host, r.port)) return fail(BookSendError::ConnectFailed);

  // CrossPoint discards a partial upload itself; stock keeps whatever arrived, so delete it.
  const auto abortUpload = [&](const BookSendError error) {
    transport.close();
    if (r.firmware == PeerFirmware::Stock) {
      HttpResponse ignored;
      const std::string deleteTarget = "/edit?path=" + urlEncode(joinPath(dir, safeName));
      request(transport, r.host, r.port, requestHead("DELETE", deleteTarget, r.host, nullptr, 0), {}, PROBE_TIMEOUT_MS,
              ignored);
    }
    return fail(error);
  };

  if (!writeString(transport,
                   requestHead("POST", target.c_str(), r.host, multipartContentType().c_str(), contentLength)) ||
      !writeString(transport, partHead)) {
    return abortUpload(BookSendError::WriteFailed);
  }

  uint32_t sent = 0;
  if (progress) progress(progressCtx, 0, r.size);
  while (sent < r.size) {
    if (cancel && *cancel) return abortUpload(BookSendError::Cancelled);
    const size_t want = (r.size - sent) < bufferSize ? (r.size - sent) : bufferSize;
    const int n = source.read(buffer, want);
    if (n <= 0) return abortUpload(BookSendError::SourceReadFailed);
    if (!transport.write(buffer, static_cast<size_t>(n))) return abortUpload(BookSendError::WriteFailed);
    sent += static_cast<uint32_t>(n);
    if (progress) progress(progressCtx, sent, r.size);
  }

  if (!writeString(transport, partTail)) return abortUpload(BookSendError::WriteFailed);

  HttpResponse resp;
  const bool answered = readResponse(transport, RESPONSE_TIMEOUT_MS, resp);
  transport.close();
  if (!answered) return fail(BookSendError::NoResponse);
  if (resp.status >= 200 && resp.status < 300) {
    BookSendResult ok;
    ok.httpStatus = resp.status;
    ok.message = resp.body;
    return ok;
  }
  const bool exists = resp.body.rfind("File already exists", 0) == 0;
  return fail(exists ? BookSendError::AlreadyExists : BookSendError::Rejected, resp.status, resp.body);
}
