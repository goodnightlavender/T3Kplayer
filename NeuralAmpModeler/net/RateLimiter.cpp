// RateLimiter.cpp — implementation. See RateLimiter.h.

#include "RateLimiter.h"

#include <algorithm>

#include "Logging.h"

namespace t3k::net {

namespace {

constexpr std::chrono::milliseconds kRefillPoll{200};  // wake-up cadence

}  // namespace

RateLimiter::RateLimiter(int capacity, int refillsPerMinute)
: mTokens(capacity > 0 ? capacity : 1)
, mCapacity(capacity > 0 ? capacity : 1)
, mTokensPerSec(refillsPerMinute > 0
                    ? static_cast<double>(refillsPerMinute) / 60.0
                    : 1.0 / 60.0)
, mLastRefill(std::chrono::steady_clock::now())
{
}

void RateLimiter::refillLocked()
{
  const auto now = std::chrono::steady_clock::now();
  const double elapsedSec =
      std::chrono::duration_cast<std::chrono::duration<double>>(
          now - mLastRefill).count();
  if (elapsedSec <= 0.0) return;

  const double refill = elapsedSec * mTokensPerSec;
  if (refill < 1.0) return;               // wait until a whole token accrues

  const int whole = static_cast<int>(refill);
  mTokens = std::min(mCapacity, mTokens + whole);

  // Advance mLastRefill by the integer-token portion so fractional
  // overshoot accrues toward the next refill.
  const double consumedSec = static_cast<double>(whole) / mTokensPerSec;
  mLastRefill += std::chrono::duration_cast<
                     std::chrono::steady_clock::duration>(
                     std::chrono::duration<double>(consumedSec));
}

bool RateLimiter::acquire(const CancellationToken& token)
{
  std::unique_lock<std::mutex> lk(mMtx);
  for (;;) {
    if (token.isCanceled()) return false;
    refillLocked();
    if (mTokens > 0) {
      --mTokens;
      return true;
    }
    // Sleep until a refill is plausible — wake every 200ms to recheck
    // cancellation. We don't compute a tight wait because mTokensPerSec
    // can be very small (one token per second at the 60/min default)
    // and the polling cost is negligible relative to a network round
    // trip.
    mCv.wait_for(lk, kRefillPoll, [this, &token]() {
      return token.isCanceled();
    });
  }
}

int RateLimiter::tokens()
{
  std::lock_guard<std::mutex> lk(mMtx);
  refillLocked();
  return mTokens;
}

}  // namespace t3k::net
