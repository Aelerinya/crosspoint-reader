#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "BookSender.h"

namespace {

class SocketTransport : public BookSendTransport {
 public:
  bool connect(const char* host, uint16_t port) override {
    fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
      close();
      return false;
    }
    return true;
  }
  bool write(const uint8_t* data, size_t len) override {
    while (len > 0) {
      const ssize_t n = ::send(fd, data, len, MSG_NOSIGNAL);
      if (n <= 0) return false;
      data += n;
      len -= static_cast<size_t>(n);
    }
    return true;
  }
  int read(uint8_t* buf, size_t len, uint32_t timeoutMs) override {
    pollfd p{fd, POLLIN, 0};
    if (::poll(&p, 1, static_cast<int>(timeoutMs)) <= 0) return 0;
    const ssize_t n = ::recv(fd, buf, len, 0);
    return n <= 0 ? -1 : static_cast<int>(n);
  }
  void close() override {
    if (fd >= 0) ::close(fd);
    fd = -1;
  }
  int fd = -1;
};

class MemorySource : public BookSendSource {
 public:
  explicit MemorySource(std::string d) : data(std::move(d)) {}
  int read(uint8_t* buf, size_t len) override {
    const size_t n = std::min(len, data.size() - pos);
    memcpy(buf, data.data() + pos, n);
    pos += n;
    return static_cast<int>(n);
  }
  std::string data;
  size_t pos = 0;
};

struct Captured {
  std::string method, target, headers, body;
};

// Loopback HTTP server: reads each request fully (by Content-Length), records it,
// answers with handler(request) and closes, like the Arduino WebServer.
class FakeServer {
 public:
  using Handler = std::function<std::string(const Captured&)>;
  explicit FakeServer(Handler h) : handler(std::move(h)) {
    listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    socklen_t len = sizeof(addr);
    getsockname(listenFd, reinterpret_cast<sockaddr*>(&addr), &len);
    port = ntohs(addr.sin_port);
    ::listen(listenFd, 8);
    worker = std::thread([this] { run(); });
  }
  ~FakeServer() {
    stopping = true;
    ::shutdown(listenFd, SHUT_RDWR);
    ::close(listenFd);
    worker.join();
  }
  std::vector<Captured> requests() {
    std::lock_guard<std::mutex> lock(mutex);
    return captured;
  }
  uint16_t port = 0;

 private:
  void run() {
    while (!stopping) {
      const int c = ::accept(listenFd, nullptr, nullptr);
      if (c < 0) return;
      std::string raw;
      char buf[4096];
      size_t headerEnd = std::string::npos;
      size_t contentLength = 0;
      while (true) {
        const ssize_t n = ::recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));
        if (headerEnd == std::string::npos) {
          headerEnd = raw.find("\r\n\r\n");
          if (headerEnd != std::string::npos) {
            const size_t cl = raw.find("Content-Length: ");
            if (cl != std::string::npos && cl < headerEnd) contentLength = std::stoul(raw.substr(cl + 16));
          }
        }
        if (headerEnd != std::string::npos && raw.size() >= headerEnd + 4 + contentLength) break;
      }
      Captured req;
      if (headerEnd != std::string::npos) {
        const size_t sp1 = raw.find(' ');
        const size_t sp2 = raw.find(' ', sp1 + 1);
        req.method = raw.substr(0, sp1);
        req.target = raw.substr(sp1 + 1, sp2 - sp1 - 1);
        req.headers = raw.substr(0, headerEnd);
        req.body = raw.substr(headerEnd + 4);
      }
      const std::string response = handler(req);
      {
        std::lock_guard<std::mutex> lock(mutex);
        captured.push_back(req);
      }
      ::send(c, response.data(), response.size(), MSG_NOSIGNAL);
      ::close(c);
    }
  }
  Handler handler;
  int listenFd = -1;
  std::thread worker;
  bool stopping = false;
  std::mutex mutex;
  std::vector<Captured> captured;
};

std::string reply(int status, const std::string& body) {
  return "HTTP/1.1 " + std::to_string(status) +
         " X\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(body.size()) +
         "\r\nConnection: close\r\n\r\n" + body;
}

// Answers like stock Xteink firmware: /status yes, /api/status no.
std::string stockHandler(const Captured& r) {
  if (r.method == "GET" && r.target == "/status") return reply(200, "{\"ok\":1}");
  if (r.target == "/edit") return reply(200, "");
  return reply(404, "too bad! Unable to find the page");
}

std::string crossPointHandler(const Captured& r) {
  if (r.method == "GET" && r.target == "/api/status") return reply(200, "{\"firmwareFamily\":\"crosspoint\"}");
  if (r.target == "/mkdir") return reply(200, "Folder created");
  if (r.target.rfind("/upload", 0) == 0) return reply(200, "File uploaded successfully: x");
  return reply(404, "Not found");
}

std::string makeBook(size_t size) {
  std::string s(size, '\0');
  for (size_t i = 0; i < size; i++) s[i] = static_cast<char>((i * 31 + 7) & 0xFF);
  return s;
}

BookSendResult sendBook(FakeServer& server, PeerFirmware fw, const std::string& book, const char* name,
                        bool overwrite = false, std::vector<uint32_t>* progress = nullptr) {
  SocketTransport transport;
  MemorySource source(book);
  BookSendRequest req;
  req.host = "127.0.0.1";
  req.port = server.port;
  req.firmware = fw;
  req.fileName = name;
  req.size = static_cast<uint32_t>(book.size());
  req.overwrite = overwrite;
  uint8_t buffer[4096];
  return BookSender::send(
      transport, source, req, buffer, sizeof(buffer),
      progress ? [](void* ctx, uint32_t sent, uint32_t) { static_cast<std::vector<uint32_t>*>(ctx)->push_back(sent); }
               : nullptr,
      progress);
}

// Returns the file part's bytes and checks the multipart framing around them.
std::string filePart(const Captured& r, std::string& disposition) {
  const size_t headEnd = r.body.find("\r\n\r\n");
  EXPECT_NE(headEnd, std::string::npos);
  const std::string head = r.body.substr(0, headEnd);
  const size_t d = head.find("Content-Disposition: ");
  disposition = head.substr(d, head.find("\r\n", d) - d);
  const std::string boundary = head.substr(2, head.find("\r\n") - 2);
  const std::string tail = "\r\n--" + boundary + "--\r\n";
  EXPECT_EQ(r.body.substr(r.body.size() - tail.size()), tail);
  return r.body.substr(headEnd + 4, r.body.size() - headEnd - 4 - tail.size());
}

}  // namespace

TEST(BookSender, FindsKnownHotspots) {
  const auto* stock = findPeerHotspot("E-Paper");
  ASSERT_NE(stock, nullptr);
  EXPECT_EQ(stock->firmware, PeerFirmware::Stock);
  EXPECT_STREQ(stock->password, "12345678");
  const auto* cp = findPeerHotspot("CrossPoint-Reader");
  ASSERT_NE(cp, nullptr);
  EXPECT_EQ(cp->firmware, PeerFirmware::CrossPoint);
  EXPECT_EQ(cp->password, nullptr);
  EXPECT_EQ(findPeerHotspot("Home WiFi"), nullptr);
}

TEST(BookSender, Helpers) {
  EXPECT_EQ(BookSender::parseStatusLine("HTTP/1.1 200 OK"), 200);
  EXPECT_EQ(BookSender::parseStatusLine("HTTP/1.0 400 Bad"), 400);
  EXPECT_EQ(BookSender::parseStatusLine("garbage"), 0);
  EXPECT_EQ(BookSender::urlEncode("/Books/Été & co"), "/Books/%C3%89t%C3%A9%20%26%20co");
  EXPECT_EQ(BookSender::headerSafeFileName("a\"b\\c\r.epub"), "a_b_c_.epub");
}

TEST(BookSender, DetectsFirmware) {
  FakeServer stock(stockHandler);
  FakeServer cp(crossPointHandler);
  FakeServer other([](const Captured&) { return reply(404, ""); });
  SocketTransport t;
  EXPECT_EQ(BookSender::detect(t, "127.0.0.1", stock.port), PeerFirmware::Stock);
  EXPECT_EQ(BookSender::detect(t, "127.0.0.1", cp.port), PeerFirmware::CrossPoint);
  EXPECT_EQ(BookSender::detect(t, "127.0.0.1", other.port), PeerFirmware::Unknown);
  // A captive portal answering every path is never taken for CrossPoint.
  FakeServer portal([](const Captured&) { return reply(200, "<html><body>Log in</body></html>"); });
  EXPECT_NE(BookSender::detect(t, "127.0.0.1", portal.port), PeerFirmware::CrossPoint);
}

TEST(BookSender, UploadsToStockLikeXteinkWebApp) {
  FakeServer server(stockHandler);
  const std::string book = makeBook(10000);
  std::vector<uint32_t> progress;
  const auto result = sendBook(server, PeerFirmware::Stock, book, "Ursula K. Le Guin - Omelas.epub", false, &progress);
  EXPECT_EQ(result.error, BookSendError::Ok);
  EXPECT_EQ(result.httpStatus, 200);

  const auto reqs = server.requests();
  ASSERT_EQ(reqs.size(), 2u);
  EXPECT_EQ(reqs[0].method, "PUT");
  EXPECT_EQ(reqs[0].target, "/edit");
  EXPECT_NE(reqs[0].body.find("name=\"path\"\r\n\r\n/Books/\r\n"), std::string::npos);

  EXPECT_EQ(reqs[1].method, "POST");
  EXPECT_EQ(reqs[1].target, "/edit");
  EXPECT_NE(reqs[1].headers.find("multipart/form-data; boundary="), std::string::npos);
  std::string disposition;
  EXPECT_EQ(filePart(reqs[1], disposition), book);
  EXPECT_EQ(disposition,
            "Content-Disposition: form-data; name=\"data\"; filename=\"/Books/Ursula K. Le Guin - Omelas.epub\"");
  ASSERT_FALSE(progress.empty());
  EXPECT_EQ(progress.front(), 0u);
  EXPECT_EQ(progress.back(), book.size());
}

TEST(BookSender, UploadsToCrossPoint) {
  FakeServer server(crossPointHandler);
  const std::string book = makeBook(9000);
  const auto result = sendBook(server, PeerFirmware::CrossPoint, book, "Éloge.epub", true);
  EXPECT_EQ(result.error, BookSendError::Ok);

  const auto reqs = server.requests();
  ASSERT_EQ(reqs.size(), 2u);
  EXPECT_EQ(reqs[0].target, "/mkdir");
  EXPECT_EQ(reqs[0].body, "name=Books&path=/");
  EXPECT_EQ(reqs[1].target, "/upload?path=/Books&overwrite=1");
  std::string disposition;
  EXPECT_EQ(filePart(reqs[1], disposition), book);
  EXPECT_EQ(disposition, "Content-Disposition: form-data; name=\"file\"; filename=\"Éloge.epub\"");
}

TEST(BookSender, ReportsExistingFile) {
  FakeServer server([](const Captured& r) {
    if (r.target.rfind("/upload", 0) == 0) return reply(400, "File already exists: a.epub");
    return reply(400, "Folder already exists");
  });
  const auto result = sendBook(server, PeerFirmware::CrossPoint, makeBook(100), "a.epub");
  EXPECT_EQ(result.error, BookSendError::AlreadyExists);
  EXPECT_EQ(result.httpStatus, 400);
  EXPECT_EQ(server.requests().back().target, "/upload?path=/Books");
}

TEST(BookSender, ReportsRejection) {
  FakeServer server([](const Captured& r) {
    if (r.target.rfind("/upload", 0) == 0) return reply(400, "Failed to create file on SD card");
    return reply(200, "");
  });
  const auto result = sendBook(server, PeerFirmware::CrossPoint, makeBook(100), "a.epub");
  EXPECT_EQ(result.error, BookSendError::Rejected);
  EXPECT_EQ(result.message, "Failed to create file on SD card");
}

TEST(BookSender, DeletesPartialFileOnStockAfterInterruptedUpload) {
  FakeServer server(stockHandler);
  SocketTransport t;
  MemorySource source(makeBook(5000));  // shorter than the declared size: the read fails midway
  BookSendRequest req;
  req.host = "127.0.0.1";
  req.port = server.port;
  req.firmware = PeerFirmware::Stock;
  req.fileName = "Été.epub";
  req.size = 8000;
  uint8_t buffer[1024];
  EXPECT_EQ(BookSender::send(t, source, req, buffer, sizeof(buffer)).error, BookSendError::SourceReadFailed);
  const auto reqs = server.requests();
  ASSERT_GE(reqs.size(), 2u);
  EXPECT_EQ(reqs.back().method, "DELETE");
  EXPECT_EQ(reqs.back().target, "/edit?path=/Books/%C3%89t%C3%A9.epub");
}

TEST(BookSender, NoDeleteOnCrossPointAfterInterruptedUpload) {
  FakeServer server(crossPointHandler);
  SocketTransport t;
  MemorySource source(makeBook(5000));
  BookSendRequest req;
  req.host = "127.0.0.1";
  req.port = server.port;
  req.firmware = PeerFirmware::CrossPoint;
  req.fileName = "a.epub";
  req.size = 8000;
  uint8_t buffer[1024];
  EXPECT_EQ(BookSender::send(t, source, req, buffer, sizeof(buffer)).error, BookSendError::SourceReadFailed);
  for (const auto& r : server.requests()) EXPECT_NE(r.method, "DELETE");
}

TEST(BookSender, CancelStopsBeforeUpload) {
  FakeServer server(crossPointHandler);
  SocketTransport t;
  MemorySource source(makeBook(100));
  BookSendRequest req;
  req.host = "127.0.0.1";
  req.port = server.port;
  req.firmware = PeerFirmware::CrossPoint;
  req.fileName = "a.epub";
  req.size = 100;
  volatile bool cancel = true;
  uint8_t buffer[64];
  EXPECT_EQ(BookSender::send(t, source, req, buffer, sizeof(buffer), nullptr, nullptr, &cancel).error,
            BookSendError::Cancelled);
}

TEST(BookSender, ConnectFailure) {
  SocketTransport t;
  MemorySource source("x");
  BookSendRequest req;
  req.host = "127.0.0.1";
  req.port = 1;  // nothing listens there
  req.firmware = PeerFirmware::Stock;
  req.fileName = "a.epub";
  req.size = 1;
  uint8_t buffer[16];
  EXPECT_EQ(BookSender::send(t, source, req, buffer, sizeof(buffer)).error, BookSendError::ConnectFailed);
}

// Sends to a real receiver when BOOK_SENDER_E2E_PORT is set (e.g. the simulator's
// CrossPoint web server). BOOK_SENDER_E2E_FILE names the book to send.
TEST(BookSender, EndToEndAgainstRealServer) {
  const char* portEnv = std::getenv("BOOK_SENDER_E2E_PORT");
  const char* fileEnv = std::getenv("BOOK_SENDER_E2E_FILE");
  if (!portEnv || !fileEnv) GTEST_SKIP() << "set BOOK_SENDER_E2E_PORT and BOOK_SENDER_E2E_FILE";
  FILE* f = fopen(fileEnv, "rb");
  ASSERT_NE(f, nullptr);
  std::string book;
  char buf[8192];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) book.append(buf, n);
  fclose(f);
  const char* slash = strrchr(fileEnv, '/');
  const std::string name = slash ? slash + 1 : fileEnv;

  SocketTransport t;
  const auto port = static_cast<uint16_t>(atoi(portEnv));
  const PeerFirmware fw = BookSender::detect(t, "127.0.0.1", port);
  ASSERT_NE(fw, PeerFirmware::Unknown);
  MemorySource source(book);
  BookSendRequest req;
  req.host = "127.0.0.1";
  req.port = port;
  req.firmware = fw;
  req.fileName = name.c_str();
  req.size = static_cast<uint32_t>(book.size());
  req.overwrite = true;
  uint8_t chunk[4096];
  const auto result = BookSender::send(t, source, req, chunk, sizeof(chunk));
  EXPECT_EQ(result.error, BookSendError::Ok) << result.httpStatus << " " << result.message;
}
