// RateLimiter.h — token-bucket rate limiter.
//
// Owned by HttpClient. Every worker job calls acquire(token) before
// issuing the WinHTTP request; if the bucket is empty the worker
// sleeps on a condition_variable until a token refills, or until the
// caller flips the cancellation token.
//
// Defaults: 100 tokens (the bucket capacity) refilling at the same
// rate per minute. Tune in the constructor if Phase 6 needs different
// shape — Phase 4 doesn't ship hot endpoints.

#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "CancellationToken.h"

namespace t3k::net {

class RateLimiter {
 public:
  // capacity        : max tokens in the bucket
  // refillsPerMinute: how many tokens refill per minute (steady-state
  //                   ceiling on requests/minute)
  RateLimiter(int capacity = 100, int refillsPerMinute = 100);

  // Block until a token is available OR the cancellation token fires.
  // Returns true if a token was consumed; false on cancel.
  bool acquire(const CancellationToken& token);

  // Snapshot of remaining tokens (after a refill pass). For
  // diagnostics only.
  int  tokens();

 private:
  // Apply any time-based refill since mLastRefill. Caller must hold
  // mMtx.
  void refillLocked();

  std::mutex                              mMtx;
  std::condition_variable                 mCv;
  int                                     mTokens;
  int                                     mCapacity;
  double                                  mTokensPerSec;
  std::chrono::steady_clock::time_point   mLastRefill;
};

}  // namespace t3k::net
