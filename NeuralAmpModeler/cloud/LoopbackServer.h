// LoopbackServer.h — minimal Winsock2 HTTP listener for the Phase 5
// OAuth callback (`http://127.0.0.1:53000/callback?code=...&state=...`).
//
// Lifecycle: one start() → one awaitCallback() → done. The server binds
// to 127.0.0.1 on the first available port in [kPortStart, kPortStart+9]
// (53000-53009) using SO_REUSEADDR so a recently-closed port can be
// re-bound across plug-in reloads. A dedicated background thread owns
// the listening socket; awaitCallback() blocks on a condition variable
// until the thread receives a request OR stop() forces shutdown.
//
// HTTP handling is intentionally bare: accept → recv up to 4 KB → parse
// the request line → split query string → send the canned 200 OK +
// HTML body → closesocket. No keep-alive, no chunked encoding, no
// multiple connections.
//
// WSAStartup is reference-counted via a tiny RAII wrapper so the server
// can be constructed and destructed repeatedly without leaking Winsock.
//
// Link: ws2_32.lib.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace t3k::cloud {

struct LoopbackResult {
  bool success = false;
  // Parsed query parameters (key → value, both URL-decoded).
  // Typically: {"code", "..."} + {"state", "..."} on success,
  // {"error", "..."} on user-cancel.
  std::map<std::string, std::string> queryParams;
  std::string error_message;
  int portUsed = 0;  // 53000-53009, or 0 if start() failed
};

class LoopbackServer {
public:
  LoopbackServer();
  ~LoopbackServer();

  LoopbackServer(const LoopbackServer&) = delete;
  LoopbackServer& operator=(const LoopbackServer&) = delete;

  // Bind to 127.0.0.1 on the first available port in
  // [startPort, startPort+9]. Returns true if a port was bound and the
  // worker thread is now waiting for the browser to connect. Returns
  // false if all ports are in use OR Winsock failed.
  bool start(int startPort = 53000);

  // The bound port. 0 if not started.
  int port() const { return mPortUsed; }

  // Block up to `timeoutMs` for the worker thread to serve one request.
  // On success the returned LoopbackResult has `success == true` and
  // `queryParams` populated. On timeout/cancel, `success == false` and
  // `error_message` carries detail. Either way, the listening socket
  // is closed before this returns.
  LoopbackResult awaitCallback(int timeoutMs);

  // Forces the worker thread to exit (closes the listening socket from
  // a different thread to break a blocked `accept`). Idempotent.
  void stop();

private:
  void workerLoop();

  // Send the canned success-page HTML to `client`. Caller closes the
  // socket.
  void writeSuccessResponse(uintptr_t clientSocket);

  std::thread mThread;
  std::atomic<bool> mShouldStop{false};

  // Listening socket. Stored as uintptr_t so the header doesn't need to
  // include <winsock2.h> (SOCKET is a uintptr_t under the hood). The
  // .cpp casts to/from SOCKET as needed. INVALID_SOCKET (~0) when
  // closed.
  uintptr_t mListener = ~static_cast<uintptr_t>(0);

  int mPortUsed = 0;

  // Result is published by the worker thread, consumed by
  // awaitCallback() on whichever thread called it. The cv lets the
  // waiter wake when the result arrives or stop() fires.
  std::mutex mMtx;
  std::condition_variable mCv;
  bool mReady = false;
  LoopbackResult mResult;
};

}  // namespace t3k::cloud
