// LibrarySync.h — Phase 8 cross-device library sync client.
//
// Connects to the CloudFlare Worker at SyncConfig::kLibrarySyncUrl
// and:
//   - On Session::SignedIn → GET /v1/library → upsert every returned
//     row into LibraryDb (so the Library tab refreshes with the
//     user's prior downloads).
//   - On EventBus::ModelAdded / ModelUpdated → PUT the row to the
//     Worker so other devices see it.
//
// When SyncConfig::isConfigured() returns false (the URL is still
// REPLACE_ME), every public method is a no-op. The plug-in builds
// and runs identically to Phase 7 until the user deploys their own
// Worker and pastes the URL.
//
// Threading: all network operations go through net::HttpClient,
// which queues onto its own worker pool. The Session and EventBus
// listeners fire on whatever thread published their event — usually
// the GUI thread for Session, a Phase 7 worker thread for EventBus.
// LibrarySync just hands off to HttpClient; no GUI-thread work
// happens here.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace t3k::library { struct ModelRow; }

namespace t3k::cloud::sync {

class LibrarySync {
public:
  // Meyers singleton. Force-initialised by NeuralAmpModeler.cpp
  // alongside Session so listeners attach before first paint.
  static LibrarySync& instance();

  // Subscribe to Session + EventBus. Idempotent; safe to call from
  // any thread. If currently signed in, also kicks off an initial
  // pullLibrary().
  void start();

  // Drop both subscriptions. Idempotent. Pending HTTP requests
  // continue but their completions become no-ops (we check the
  // running flag).
  void stop();

  // Push one library row to the Worker. Fires HTTP asynchronously;
  // completion logs errors but otherwise has no visible effect.
  // No-op when !isConfigured() or when not signed in.
  void pushEntry(const ::t3k::library::ModelRow& row);
  void pushPreset(int64_t presetId);
  void deletePreset(int64_t presetId);
  void pullPresets();

  // Fetch the user's entire library from the Worker and upsert each
  // entry into LibraryDb. onDone fires on the HTTP worker thread.
  // No-op when !isConfigured() or when not signed in.
  using PullCompletion = std::function<void(bool ok, int entries)>;
  void pullLibrary(PullCompletion onDone = {});

  // Persistent listener for every pull (both auto-triggered ones from
  // the Session::SignedIn event and explicit pullLibrary() calls).
  // ToneRoot registers one to know when to show the restore modal.
  // Fires on the HTTP worker thread.
  using PullListener = std::function<void(bool ok, int entries)>;
  void setPullListener(PullListener cb);

  // Count LibraryDb rows with missing=1. These are tones the user has
  // in their cloud library but doesn't have on disk yet — typically
  // because they signed in on a new machine and pullLibrary just
  // hydrated stub rows. Safe to call from the GUI thread.
  int countLocalMissing() const;

  // Walk every missing row, deduplicate by tone_id, and queue a
  // Phase-7 Downloader::enqueueTone for each unique tone after
  // fetching the full Tone metadata via Tone3000Client::getTone.
  // onDone fires after all enqueueTone calls have been kicked off
  // (NOT after the downloads themselves complete — those run async
  // through the existing Downloader pipeline and surface via the
  // Cloud-tab status banner + per-card pill state).
  using RestoreCompletion = std::function<void(int queued, int failed)>;
  void restoreAllMissing(RestoreCompletion onDone = {});

private:
  LibrarySync() = default;
  ~LibrarySync() = default;
  LibrarySync(const LibrarySync&) = delete;
  LibrarySync& operator=(const LibrarySync&) = delete;

  // Build the per-entry PUT URL: <base>/v1/library/entry/<tone>/<model>
  std::string entryUrl(const std::string& tone_id,
                       const std::string& model_id) const;

  // Build a JSON body from a ModelRow for PUT.
  std::string entryJson(const ::t3k::library::ModelRow& row) const;

  std::atomic<bool> mRunning{false};
  std::atomic<bool> mSubscribed{false};

  int mSessionListenerId  = 0;
  int mEventBusListenerId = 0;

  mutable std::mutex mMtx;  // covers listener-id + mPullListener
  PullListener mPullListener;
};

}  // namespace t3k::cloud::sync
