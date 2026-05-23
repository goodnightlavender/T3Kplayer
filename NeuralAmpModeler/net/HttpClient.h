// HttpClient.h — async HTTP client backed by WinHTTP.
//
// Public surface for everything that needs to talk HTTPS: Phase 5 OAuth,
// Phase 6 Cloud-tab endpoints, Phase 7 thumbnail downloads, the Phase 4
// "Test net" diagnostic button.
//
// The client owns a WorkerPool of std::thread workers (2-4, sized from
// hardware_concurrency). Every `send()` enqueues a job, returns a
// CancellationToken to the caller, and resolves on a worker thread by
// invoking the supplied completion lambda. The completion runs on the
// worker thread — UI callers must marshal back to the GUI themselves
// (typically via a mutex + the next iPlug2 paint cycle).
//
// Spec deviation: the design doc (§5 Phase 4) called for libcurl +
// Schannel, statically linked. We use WinHTTP instead — already part
// of every Windows install, HTTPS via Schannel out of the box, no
// vendoring needed. The interface here is libcurl-swap-friendly for
// future Mac/Linux ports.

#pragma once

#include <cstddef>
#include <functional>
#include <memory>

#include "CancellationToken.h"
#include "HttpRequest.h"
#include "HttpResponse.h"

namespace t3k::net {

class WorkerPool;
class RateLimiter;

class HttpClient {
 public:
  using Completion = std::function<void(const HttpResponse&)>;

  // Meyers singleton. First touch constructs the WorkerPool + initial
  // WinHTTP session; subsequent calls return the same instance.
  static HttpClient& instance();

  // Submit a request. The completion fires on a worker thread when
  // the request terminates — success, HTTP-error, transport-error, or
  // cancellation. Returned token can be flipped at any point; the
  // worker checks before sending and after each WinHttpReadData chunk.
  CancellationToken send(HttpRequest req, Completion onDone);

  std::size_t poolSize() const;
  std::size_t pendingJobs() const;

 private:
  HttpClient();
  ~HttpClient();
  HttpClient(const HttpClient&) = delete;
  HttpClient& operator=(const HttpClient&) = delete;

  std::unique_ptr<WorkerPool>  mPool;
  std::unique_ptr<RateLimiter> mLimiter;
};

}  // namespace t3k::net
