// LoopbackServer.cpp — Winsock2 implementation. See LoopbackServer.h.
//
// Design notes:
//   - WSAStartup/WSACleanup are refcounted (per MSDN). We wrap them in
//     a tiny global counter so multiple LoopbackServer instances across
//     the plug-in's lifetime don't leak or double-cleanup.
//   - The listener owns a dedicated std::thread. The thread accepts
//     ONE connection (the user's browser hitting our callback URL);
//     after serving the request it publishes the parsed query to
//     mResult and notifies the cv.
//   - stop() closes the listening socket from a different thread,
//     which causes the blocked `accept` to return INVALID_SOCKET. The
//     worker sees mShouldStop, publishes a "stopped" result, and exits.
//   - HTTP response is a fixed-length 200 OK with text/html body. No
//     chunked encoding.

#include "LoopbackServer.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace t3k::cloud {

namespace {

// Refcounted WSAStartup/WSACleanup. The counter lives in Meyers
// statics so we don't pay DLL-load static-init costs.
std::mutex& WsaMutex()
{
  static std::mutex m;
  return m;
}

int& WsaRefcount()
{
  static int n = 0;
  return n;
}

bool WsaAcquire()
{
  std::lock_guard<std::mutex> lk(WsaMutex());
  if (WsaRefcount() > 0) {
    WsaRefcount()++;
    return true;
  }
  WSADATA data;
  if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
  WsaRefcount() = 1;
  return true;
}

void WsaRelease()
{
  std::lock_guard<std::mutex> lk(WsaMutex());
  if (WsaRefcount() <= 0) return;
  WsaRefcount()--;
  if (WsaRefcount() == 0) {
    ::WSACleanup();
  }
}

// URL-decode a single percent-encoded query value. + → space; %XX → byte.
std::string UrlDecode(const std::string& s)
{
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '+') {
      out.push_back(' ');
    } else if (c == '%' && i + 2 < s.size()) {
      auto fromHex = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
      };
      const int hi = fromHex(s[i + 1]);
      const int lo = fromHex(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
      } else {
        out.push_back(c);
      }
    } else {
      out.push_back(c);
    }
  }
  return out;
}

// Parse `k1=v1&k2=v2` into a map; both keys and values are URL-decoded.
std::map<std::string, std::string> ParseQuery(const std::string& q)
{
  std::map<std::string, std::string> out;
  size_t i = 0;
  while (i < q.size()) {
    const size_t amp = q.find('&', i);
    const size_t end = (amp == std::string::npos) ? q.size() : amp;
    const std::string pair = q.substr(i, end - i);
    const size_t eq = pair.find('=');
    if (eq == std::string::npos) {
      if (!pair.empty()) out[UrlDecode(pair)] = "";
    } else {
      out[UrlDecode(pair.substr(0, eq))] = UrlDecode(pair.substr(eq + 1));
    }
    if (amp == std::string::npos) break;
    i = amp + 1;
  }
  return out;
}

// Extract the query string out of an HTTP request-line path:
//   "GET /callback?code=abc&state=xyz HTTP/1.1"  →  "code=abc&state=xyz"
//   "GET /callback HTTP/1.1"                      →  ""
// Returns "" if no path can be parsed.
std::string ExtractQuery(const std::string& requestLine)
{
  const size_t firstSpace = requestLine.find(' ');
  if (firstSpace == std::string::npos) return {};
  const size_t secondSpace = requestLine.find(' ', firstSpace + 1);
  if (secondSpace == std::string::npos) return {};
  const std::string uri = requestLine.substr(firstSpace + 1,
                                             secondSpace - firstSpace - 1);
  const size_t q = uri.find('?');
  if (q == std::string::npos) return {};
  return uri.substr(q + 1);
}

constexpr const char* kSuccessHtml =
    "<!DOCTYPE html>\r\n"
    "<html><head><title>TONE3000 Player</title></head>\r\n"
    "<body style=\"font-family:sans-serif;text-align:center;"
    "margin-top:5em;background:#000;color:#fff\">\r\n"
    "  <h2>Signed in</h2>\r\n"
    "  <p>You can close this tab and return to your DAW.</p>\r\n"
    "</body></html>\r\n";

}  // namespace

LoopbackServer::LoopbackServer() = default;

LoopbackServer::~LoopbackServer()
{
  stop();
  if (mThread.joinable()) mThread.join();
  if (mPortUsed != 0) {
    // Pair the WsaAcquire() from start() if it succeeded.
    WsaRelease();
  }
}

bool LoopbackServer::start(int startPort)
{
  if (!WsaAcquire()) return false;

  // Walk [startPort, startPort+9] looking for a free port.
  SOCKET listener = INVALID_SOCKET;
  int boundPort = 0;
  for (int p = startPort; p < startPort + 10; ++p) {
    listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET) continue;

    BOOL yes = TRUE;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);  // 127.0.0.1
    addr.sin_port = ::htons(static_cast<u_short>(p));

    if (::bind(listener,
               reinterpret_cast<sockaddr*>(&addr),
               sizeof(addr)) == 0) {
      if (::listen(listener, 1) == 0) {
        boundPort = p;
        break;
      }
    }
    ::closesocket(listener);
    listener = INVALID_SOCKET;
  }

  if (listener == INVALID_SOCKET || boundPort == 0) {
    WsaRelease();
    return false;
  }

  mListener = static_cast<uintptr_t>(listener);
  mPortUsed = boundPort;
  mShouldStop = false;
  mReady = false;

  mThread = std::thread([this]() { workerLoop(); });
  return true;
}

LoopbackResult LoopbackServer::awaitCallback(int timeoutMs)
{
  std::unique_lock<std::mutex> lk(mMtx);
  const auto deadline =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(timeoutMs);

  mCv.wait_until(lk, deadline, [this]() { return mReady; });

  if (!mReady) {
    // Timeout. Force the worker to exit so we can join cleanly.
    lk.unlock();
    stop();
    if (mThread.joinable()) mThread.join();
    LoopbackResult timeoutRes;
    timeoutRes.success = false;
    timeoutRes.error_message = "timeout waiting for OAuth callback";
    timeoutRes.portUsed = mPortUsed;
    return timeoutRes;
  }

  LoopbackResult out = mResult;
  out.portUsed = mPortUsed;
  lk.unlock();

  // Worker has already exited or is about to exit by the time mReady is
  // set; join here for a clean teardown.
  if (mThread.joinable()) mThread.join();
  return out;
}

void LoopbackServer::stop()
{
  mShouldStop = true;
  if (mListener != ~static_cast<uintptr_t>(0)) {
    // Closing the listening socket from a different thread breaks the
    // blocked `accept`.
    ::closesocket(static_cast<SOCKET>(mListener));
    mListener = ~static_cast<uintptr_t>(0);
  }
  // Nudge anyone waiting on the cv.
  {
    std::lock_guard<std::mutex> lk(mMtx);
    if (!mReady) {
      mReady = true;
      mResult.success = false;
      mResult.error_message = "loopback server stopped";
    }
  }
  mCv.notify_all();
}

void LoopbackServer::workerLoop()
{
  // Accept ONE connection. The OAuth flow points the browser at us
  // exactly once.
  SOCKET listener = static_cast<SOCKET>(mListener);
  if (listener == INVALID_SOCKET) return;

  sockaddr_in clientAddr{};
  int clientLen = sizeof(clientAddr);
  SOCKET client = ::accept(listener,
                           reinterpret_cast<sockaddr*>(&clientAddr),
                           &clientLen);

  if (client == INVALID_SOCKET) {
    // Either stop() closed the listener or accept failed. Either way,
    // we're done.
    std::lock_guard<std::mutex> lk(mMtx);
    if (!mReady) {
      mReady = true;
      mResult.success = false;
      mResult.error_message = mShouldStop
          ? "loopback server stopped"
          : "accept failed";
    }
    mCv.notify_all();
    return;
  }

  // Read the HTTP request. A real browser sends a complete request in
  // a single TCP segment for the trivial GET; if we don't see "\r\n\r\n"
  // within 8 KB we bail.
  std::vector<char> buf;
  buf.reserve(4096);
  char chunk[1024];
  bool headersComplete = false;
  for (int i = 0; i < 8; ++i) {  // up to 8*1024 = 8 KB
    const int got = ::recv(client, chunk, sizeof(chunk), 0);
    if (got <= 0) break;
    buf.insert(buf.end(), chunk, chunk + got);
    const std::string view(buf.data(), buf.size());
    if (view.find("\r\n\r\n") != std::string::npos) {
      headersComplete = true;
      break;
    }
  }

  std::string requestLine;
  if (headersComplete) {
    const std::string view(buf.data(), buf.size());
    const size_t crlf = view.find("\r\n");
    if (crlf != std::string::npos) {
      requestLine = view.substr(0, crlf);
    }
  }

  // Parse the query before responding so we can publish even if the
  // socket close races the next step.
  LoopbackResult result;
  result.portUsed = mPortUsed;
  const std::string query = ExtractQuery(requestLine);
  result.queryParams = ParseQuery(query);
  if (result.queryParams.count("code") &&
      result.queryParams.count("state")) {
    result.success = true;
  } else if (result.queryParams.count("error")) {
    result.success = false;
    result.error_message =
        "OAuth provider returned error: " +
        result.queryParams["error"];
  } else if (requestLine.empty()) {
    result.success = false;
    result.error_message = "empty HTTP request";
  } else {
    result.success = false;
    result.error_message = "callback URL missing code/state params";
  }

  // Always send the canned 200 OK so the browser doesn't dangle.
  writeSuccessResponse(static_cast<uintptr_t>(client));
  ::shutdown(client, SD_BOTH);
  ::closesocket(client);

  // Close the listening socket — we've served our one request.
  if (mListener != ~static_cast<uintptr_t>(0)) {
    ::closesocket(static_cast<SOCKET>(mListener));
    mListener = ~static_cast<uintptr_t>(0);
  }

  {
    std::lock_guard<std::mutex> lk(mMtx);
    mResult = std::move(result);
    mReady = true;
  }
  mCv.notify_all();
}

void LoopbackServer::writeSuccessResponse(uintptr_t clientSocket)
{
  SOCKET client = static_cast<SOCKET>(clientSocket);

  const std::string body = kSuccessHtml;
  char header[256];
  const int n = std::snprintf(
      header, sizeof(header),
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "Content-Length: %zu\r\n"
      "Connection: close\r\n"
      "\r\n",
      body.size());

  if (n > 0 && n < static_cast<int>(sizeof(header))) {
    ::send(client, header, n, 0);
  }
  ::send(client,
         body.data(),
         static_cast<int>(body.size()),
         0);
}

}  // namespace t3k::cloud
