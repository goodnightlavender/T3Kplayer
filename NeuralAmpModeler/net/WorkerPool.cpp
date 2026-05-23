// WorkerPool.cpp — implementation. See WorkerPool.h.

#include "WorkerPool.h"

#include <algorithm>
#include <utility>

#include "Logging.h"

namespace t3k::net {

namespace {

std::size_t DefaultThreadCount()
{
  unsigned hc = std::thread::hardware_concurrency();
  if (hc == 0) hc = 2;                    // hardware_concurrency may report 0
  // Clamp to [2, 4]. The pool sees a steady trickle of HTTP jobs from
  // the GUI; more than four would just contend on the shared session.
  if (hc < 2) hc = 2;
  if (hc > 4) hc = 4;
  return static_cast<std::size_t>(hc);
}

}  // namespace

WorkerPool::WorkerPool() : WorkerPool(DefaultThreadCount()) {}

WorkerPool::WorkerPool(std::size_t numThreads)
{
  if (numThreads == 0) numThreads = 1;
  mThreads.reserve(numThreads);
  for (std::size_t i = 0; i < numThreads; ++i) {
    mThreads.emplace_back(&WorkerPool::workerLoop, this);
  }
  T3K_NET_LOG("info", "WorkerPool: %zu thread(s)", numThreads);
}

WorkerPool::~WorkerPool()
{
  {
    std::lock_guard<std::mutex> lk(mMtx);
    mShutdown.store(true, std::memory_order_release);
  }
  mCv.notify_all();
  for (auto& t : mThreads) {
    if (t.joinable()) t.join();
  }
}

bool WorkerPool::submit(std::function<void()> job)
{
  if (!job) return false;
  {
    std::lock_guard<std::mutex> lk(mMtx);
    if (mShutdown.load(std::memory_order_acquire)) return false;
    mQueue.emplace_back(std::move(job));
  }
  mCv.notify_one();
  return true;
}

std::size_t WorkerPool::pendingJobs() const
{
  std::lock_guard<std::mutex> lk(mMtx);
  return mQueue.size();
}

void WorkerPool::workerLoop()
{
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> lk(mMtx);
      mCv.wait(lk, [this]() {
        return mShutdown.load(std::memory_order_acquire) || !mQueue.empty();
      });
      if (mShutdown.load(std::memory_order_acquire) && mQueue.empty()) {
        return;
      }
      job = std::move(mQueue.front());
      mQueue.pop_front();
    }
    if (job) {
      // The job body owns its own try/catch — the pool intentionally
      // doesn't swallow exceptions silently. If something throws here
      // the process terminates, which is the correct behavior for a
      // background thread leaking exceptions.
      job();
    }
  }
}

}  // namespace t3k::net
