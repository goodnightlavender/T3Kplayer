// LibraryScanner.h — synchronous folder walker on a background thread.
//
// Phase 3 ships only the initial walk + the user-triggered "Rescan"
// path. There is NO Win32 ReadDirectoryChangesW watcher yet (deferred
// per spec §8 "Scanner" + Phase 3 out-of-scope list).
//
// Lifecycle:
//   1. rescan(root) is called from the GUI thread.
//   2. If a previous scan is still running we ignore the request
//      (mRunning is the gate).
//   3. The walk spins on a `std::thread`; previous thread is joined
//      before launching the new one to avoid leaking handles.
//   4. Events: ScanStarted → ModelAdded/Updated per file → ScanFinished.
//      LibraryView listens via EventBus.
//
// Singleton — there is exactly one scanner per process.

#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <thread>

namespace t3k::library {

class LibraryScanner {
 public:
  static LibraryScanner& instance();

  // Kick off a background walk. If rootOverride is set, that path is
  // walked; otherwise the path from Settings::instance().tone3000_root
  // is used. No-op if a scan is already in progress or the root is
  // empty.
  void rescan(std::optional<std::string> rootOverride = std::nullopt);

  bool isScanning() const { return mRunning.load(); }

 private:
  LibraryScanner() = default;
  ~LibraryScanner();
  LibraryScanner(const LibraryScanner&) = delete;
  LibraryScanner& operator=(const LibraryScanner&) = delete;

  // Synchronously walk `root`. Runs on mThread.
  void walk(std::string root);

  std::atomic<bool> mRunning{false};
  std::thread       mThread;
};

}  // namespace t3k::library
