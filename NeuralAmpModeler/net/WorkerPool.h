// WorkerPool.h — fixed-size thread pool for HTTP jobs.
//
// One pool per HttpClient (in practice one HttpClient per process —
// it's a Meyers singleton). Threads spin up at construction, sleep on
// a condition_variable when the queue is empty, and tear down cleanly
// in the dtor (shutdown flag + notify_all + join).
//
// The pool deliberately holds NO knowledge of HTTP / WinHTTP — it's a
// generic `std::function<void()>` queue. The HTTP-specific logic lives
// in the lambda submitted by HttpClient::send.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace t3k::net {

class WorkerPool {
 public:
  // Default-constructed pool sizes itself from
  // std::thread::hardware_concurrency() clamped to [2, 4]. Threads
  // start running immediately.
  WorkerPool();

  // Explicit size for tests / diagnostics.
  explicit WorkerPool(std::size_t numThreads);

  // Signals shutdown, wakes every worker, joins them all. Outstanding
  // queued jobs are dropped — workers exit before draining the queue.
  ~WorkerPool();

  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  // Append a job. Returns false if the pool is already shutting down
  // (rare in practice — the singleton outlives every caller). The job
  // is moved into the queue.
  bool submit(std::function<void()> job);

  std::size_t threadCount() const { return mThreads.size(); }

  // Best-effort snapshot of the queue depth. For diagnostics only.
  std::size_t pendingJobs() const;

 private:
  void workerLoop();

  std::vector<std::thread>          mThreads;
  mutable std::mutex                mMtx;
  std::condition_variable           mCv;
  std::deque<std::function<void()>> mQueue;
  std::atomic<bool>                 mShutdown{false};
};

}  // namespace t3k::net
