// HttpClient.cpp — WinHTTP-backed implementation. See HttpClient.h.
//
// One synchronous WinHTTP request per worker job. We use the synchronous
// mode (no WINHTTP_FLAG_ASYNC) because the WorkerPool already gives us
// off-thread execution; async WinHTTP would add a second callback-driven
// state machine for no benefit.
//
// A single global HINTERNET session is cached (lazily-initialized in
// SessionGet()) and re-used for every connection — opening a fresh
// session per request would cost a TLS handshake for nothing. Per-
// request connect + request handles are scoped via the HttpHandle RAII
// guard.
//
// Error model: any WinHTTP call that returns FALSE pops the error path —
// we populate `error_message` with a `"WinHTTP <call> failed: error N"`
// string and leave `status_code = 0`. Cancellation is handled by polling
// `token.isCanceled()` at safe points; mid-request we just close the
// request handle and let WinHTTP fail the next operation.

#include "HttpClient.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "Logging.h"
#include "WorkerPool.h"

namespace t3k::net {

namespace {

// ----- UTF-8 <-> UTF-16 --------------------------------------------------

std::wstring Widen(const std::string& s)
{
  if (s.empty()) return {};
  int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<size_t>(n), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                        static_cast<int>(s.size()), out.data(), n);
  return out;
}

std::string Narrow(const wchar_t* s, int len = -1)
{
  if (!s) return {};
  if (len < 0) len = static_cast<int>(std::wcslen(s));
  if (len == 0) return {};
  int n = ::WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0,
                                nullptr, nullptr);
  if (n <= 0) return {};
  std::string out(static_cast<size_t>(n), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, s, len, out.data(), n,
                        nullptr, nullptr);
  return out;
}

// ----- RAII handle guard -------------------------------------------------

struct HttpHandle {
  HINTERNET h = nullptr;
  HttpHandle() = default;
  explicit HttpHandle(HINTERNET handle) : h(handle) {}
  HttpHandle(const HttpHandle&) = delete;
  HttpHandle& operator=(const HttpHandle&) = delete;
  ~HttpHandle() { if (h) ::WinHttpCloseHandle(h); }
};

// ----- Method conversion -------------------------------------------------

const wchar_t* MethodToVerb(HttpMethod m)
{
  switch (m) {
    case HttpMethod::Get:    return L"GET";
    case HttpMethod::Post:   return L"POST";
    case HttpMethod::Put:    return L"PUT";
    case HttpMethod::Delete: return L"DELETE";
  }
  return L"GET";
}

// ----- Process-global WinHTTP session ------------------------------------
//
// One session handle for the whole process. WinHTTP cleans this up at
// process exit even if we never call WinHttpCloseHandle on it; we use a
// Meyers-style getter so it's initialized on first use rather than at
// static-init time (avoids DllMain ordering hazards).

HINTERNET SessionGet()
{
  // The session is owned by this static for the lifetime of the
  // process. Leaking it on shutdown is intentional — WinHTTP handles
  // cleanup, and joining the worker pool already happens via
  // ~HttpClient -> ~WorkerPool before the runtime tears down.
  static HINTERNET session = []() -> HINTERNET {
    HINTERNET s = ::WinHttpOpen(L"TONE3000-Player/0.1.0 (Phase 4)",
                                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS,
                                0);
    if (!s) {
      T3K_NET_LOG("error", "WinHttpOpen failed: %lu", ::GetLastError());
    }
    return s;
  }();
  return session;
}

// ----- Header utilities --------------------------------------------------

std::string AsciiLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return s;
}

void TrimAscii(std::string& s)
{
  auto isWS = [](unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!s.empty() && isWS(static_cast<unsigned char>(s.back()))) s.pop_back();
  std::size_t i = 0;
  while (i < s.size() && isWS(static_cast<unsigned char>(s[i]))) ++i;
  if (i > 0) s.erase(0, i);
}

// Parse the CRLF-separated header block WinHttpQueryHeaders returns.
// The first line is the HTTP status line (e.g. "HTTP/1.1 204 No Content")
// which we skip. Each subsequent line is split on the first ":"; key is
// lowercased so callers can do case-insensitive lookup.
void ParseHeaders(const std::wstring& blob,
                  std::map<std::string, std::string>& out)
{
  const std::string asUtf8 = Narrow(blob.c_str(),
                                    static_cast<int>(blob.size()));
  std::size_t pos = 0;
  bool firstLine = true;
  while (pos < asUtf8.size()) {
    std::size_t eol = asUtf8.find("\r\n", pos);
    if (eol == std::string::npos) eol = asUtf8.size();
    std::string line = asUtf8.substr(pos, eol - pos);
    pos = eol + 2;
    if (line.empty()) continue;
    if (firstLine) { firstLine = false; continue; }

    const std::size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string key = AsciiLower(line.substr(0, colon));
    std::string val = line.substr(colon + 1);
    TrimAscii(key);
    TrimAscii(val);
    if (key.empty()) continue;
    out.emplace(std::move(key), std::move(val));
  }
}

// ----- The request body --------------------------------------------------

// Synchronously issue `req` and populate `res`. Honors `token` at safe
// points. Never throws — all errors funnel into res.error_message.
void IssueRequest(const HttpRequest& req,
                  HttpResponse& res,
                  const CancellationToken& token)
{
  const auto tStart = std::chrono::steady_clock::now();

  HINTERNET session = SessionGet();
  if (!session) {
    res.error_message = "WinHttpOpen failed (session unavailable)";
    return;
  }

  // ----- Timeouts. Split the user-supplied budget four ways. ----------
  const int t = (req.timeout_ms > 0) ? req.timeout_ms : 15'000;
  const int per = t / 4;
  if (!::WinHttpSetTimeouts(session, per, per, per, per)) {
    // Non-fatal; just means we'll keep the previous timeouts. Log and
    // continue.
    T3K_NET_LOG("warn", "WinHttpSetTimeouts failed: %lu", ::GetLastError());
  }

  // ----- Crack the URL ------------------------------------------------
  // WinHttpCrackUrl wants a writable wide buffer; the URL_COMPONENTS
  // length fields are populated with pointers BACK INTO that buffer.
  std::wstring urlW = Widen(req.url);
  if (urlW.empty()) {
    res.error_message = "Empty URL";
    return;
  }

  // Push a writable copy so WinHTTP's pointers stay valid for the
  // duration of the request.
  std::vector<wchar_t> urlBuf(urlW.begin(), urlW.end());
  urlBuf.push_back(L'\0');

  URL_COMPONENTS uc;
  std::memset(&uc, 0, sizeof(uc));
  uc.dwStructSize     = sizeof(uc);
  uc.dwSchemeLength   = static_cast<DWORD>(-1);
  uc.dwHostNameLength = static_cast<DWORD>(-1);
  uc.dwUrlPathLength  = static_cast<DWORD>(-1);
  uc.dwExtraInfoLength = static_cast<DWORD>(-1);
  uc.dwUserNameLength = 0;
  uc.dwPasswordLength = 0;

  if (!::WinHttpCrackUrl(urlBuf.data(),
                         static_cast<DWORD>(urlBuf.size() - 1),
                         0, &uc)) {
    res.error_message = "WinHttpCrackUrl failed: error " +
                        std::to_string(::GetLastError());
    return;
  }

  const bool isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
  std::wstring host(uc.lpszHostName, uc.dwHostNameLength);

  // URL path may have a query string ("extra info") tacked on; WinHTTP
  // wants them together in the path arg.
  std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
  if (uc.dwExtraInfoLength > 0) {
    path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
  }
  if (path.empty()) path = L"/";

  // ----- Connect ------------------------------------------------------
  if (token.isCanceled()) {
    res.canceled = true;
    res.error_message = "canceled before connect";
    return;
  }

  HttpHandle conn(::WinHttpConnect(session, host.c_str(),
                                   uc.nPort, 0));
  if (!conn.h) {
    res.error_message = "WinHttpConnect failed: error " +
                        std::to_string(::GetLastError());
    return;
  }

  // ----- Open request -------------------------------------------------
  DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
  HttpHandle hreq(::WinHttpOpenRequest(conn.h,
                                       MethodToVerb(req.method),
                                       path.c_str(),
                                       nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       flags));
  if (!hreq.h) {
    res.error_message = "WinHttpOpenRequest failed: error " +
                        std::to_string(::GetLastError());
    return;
  }

  // ----- Add request headers ------------------------------------------
  if (!req.headers.empty()) {
    std::string blob;
    for (const auto& [k, v] : req.headers) {
      blob += k;
      blob += ": ";
      blob += v;
      blob += "\r\n";
    }
    std::wstring blobW = Widen(blob);
    if (!::WinHttpAddRequestHeaders(hreq.h, blobW.c_str(),
                                    static_cast<DWORD>(blobW.size()),
                                    WINHTTP_ADDREQ_FLAG_ADD)) {
      // Non-fatal — log and continue. Some headers may have made it
      // (WinHTTP processes them in batch); the server may still
      // respond.
      T3K_NET_LOG("warn", "WinHttpAddRequestHeaders failed: %lu",
                  ::GetLastError());
    }
  }

  if (token.isCanceled()) {
    res.canceled = true;
    res.error_message = "canceled before send";
    return;
  }

  // ----- Send ---------------------------------------------------------
  LPVOID bodyPtr  = req.body.empty()
      ? WINHTTP_NO_REQUEST_DATA
      : const_cast<uint8_t*>(req.body.data());
  DWORD  bodyLen  = static_cast<DWORD>(req.body.size());
  if (!::WinHttpSendRequest(hreq.h,
                            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            bodyPtr, bodyLen,
                            bodyLen, 0)) {
    res.error_message = "WinHttpSendRequest failed: error " +
                        std::to_string(::GetLastError());
    return;
  }

  if (!::WinHttpReceiveResponse(hreq.h, nullptr)) {
    res.error_message = "WinHttpReceiveResponse failed: error " +
                        std::to_string(::GetLastError());
    return;
  }

  // ----- Status code --------------------------------------------------
  DWORD status = 0;
  DWORD statusLen = sizeof(status);
  if (!::WinHttpQueryHeaders(hreq.h,
                             WINHTTP_QUERY_STATUS_CODE |
                                 WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &statusLen,
                             WINHTTP_NO_HEADER_INDEX)) {
    res.error_message = "WinHttpQueryHeaders(status) failed: error " +
                        std::to_string(::GetLastError());
    return;
  }
  res.status_code = static_cast<int>(status);

  // ----- Response headers ---------------------------------------------
  DWORD hdrSize = 0;
  ::WinHttpQueryHeaders(hreq.h, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        WINHTTP_NO_OUTPUT_BUFFER, &hdrSize,
                        WINHTTP_NO_HEADER_INDEX);
  if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER && hdrSize > 0) {
    std::wstring blob(hdrSize / sizeof(wchar_t), L'\0');
    if (::WinHttpQueryHeaders(hreq.h, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                              WINHTTP_HEADER_NAME_BY_INDEX,
                              blob.data(), &hdrSize,
                              WINHTTP_NO_HEADER_INDEX)) {
      ParseHeaders(blob, res.headers);
    }
  }

  // ----- Body ---------------------------------------------------------
  res.body.clear();
  for (;;) {
    if (token.isCanceled()) {
      res.canceled = true;
      res.error_message = "canceled during read";
      return;
    }
    DWORD avail = 0;
    if (!::WinHttpQueryDataAvailable(hreq.h, &avail)) {
      res.error_message = "WinHttpQueryDataAvailable failed: error " +
                          std::to_string(::GetLastError());
      return;
    }
    if (avail == 0) break;

    const std::size_t off = res.body.size();
    res.body.resize(off + avail);
    DWORD read = 0;
    if (!::WinHttpReadData(hreq.h, res.body.data() + off, avail, &read)) {
      res.error_message = "WinHttpReadData failed: error " +
                          std::to_string(::GetLastError());
      return;
    }
    if (read < avail) res.body.resize(off + read);
  }

  const auto tEnd = std::chrono::steady_clock::now();
  res.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        tEnd - tStart).count();
}

}  // namespace

// ----- HttpClient surface ------------------------------------------------

HttpClient& HttpClient::instance()
{
  static HttpClient c;
  return c;
}

HttpClient::HttpClient()
: mPool(std::make_unique<WorkerPool>())
{
  // Touch the WinHTTP session early so the first send() doesn't pay
  // the open-session latency on the worker thread.
  (void)SessionGet();
  T3K_NET_LOG("info", "HttpClient: ready (%zu workers)",
              mPool->threadCount());
}

HttpClient::~HttpClient() = default;

CancellationToken HttpClient::send(HttpRequest req, Completion onDone)
{
  CancellationToken token;
  // Capture-by-value so the worker thread owns everything.
  CancellationToken workerToken = token;
  HttpRequest reqCopy = std::move(req);
  Completion done = std::move(onDone);

  T3K_NET_LOG("debug", "send: %s",
              reqCopy.url.empty() ? "(empty url)" : reqCopy.url.c_str());

  const bool queued = mPool->submit(
      [reqCopy = std::move(reqCopy), workerToken, done = std::move(done)]() {
        HttpResponse res;
        if (workerToken.isCanceled()) {
          res.canceled = true;
          res.error_message = "canceled before dispatch";
        } else {
          IssueRequest(reqCopy, res, workerToken);
        }
        if (workerToken.isCanceled() && !res.canceled) {
          res.canceled = true;
        }
        T3K_NET_LOG("debug", "done: status=%d body=%zu canceled=%d msg=%s",
                    res.status_code, res.body.size(), int(res.canceled),
                    res.error_message.c_str());
        if (done) done(res);
      });

  if (!queued) {
    // Pool is shutting down. Fire completion synchronously with a
    // canceled response so callers don't hang waiting.
    HttpResponse res;
    res.canceled = true;
    res.error_message = "HttpClient is shutting down";
    if (done) done(res);
  }

  return token;
}

std::size_t HttpClient::poolSize() const
{
  return mPool ? mPool->threadCount() : 0;
}

std::size_t HttpClient::pendingJobs() const
{
  return mPool ? mPool->pendingJobs() : 0;
}

}  // namespace t3k::net
