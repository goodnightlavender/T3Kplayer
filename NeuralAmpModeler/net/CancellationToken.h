// CancellationToken.h — caller-flippable abort flag shared with the
// background HTTP worker.
//
// Constructed by HttpClient::send and returned by value to the caller.
// Both the caller AND the worker thread hold the same shared_ptr to an
// atomic<bool>; the worker polls isCanceled() at safe points (before
// WinHttpSendRequest, after each WinHttpReadData chunk). The caller
// flips it via cancel() — no other coordination needed.
//
// Default-constructed tokens hold a non-null flag set to false; copy-
// construction is cheap (shared_ptr inc) and intentionally allowed so
// multiple parties can observe the same cancellation.

#pragma once

#include <atomic>
#include <memory>

namespace t3k::net {

class CancellationToken {
 public:
  CancellationToken()
  : mFlag(std::make_shared<std::atomic<bool>>(false)) {}

  // Trigger cancellation. Idempotent.
  void cancel() {
    if (mFlag) mFlag->store(true, std::memory_order_release);
  }

  bool isCanceled() const {
    return mFlag && mFlag->load(std::memory_order_acquire);
  }

 private:
  std::shared_ptr<std::atomic<bool>> mFlag;
};

}  // namespace t3k::net
