// Downloader.h — orchestrates tone-download pipelines (Phase 7).
//
// For each enqueued Tone the pipeline runs:
//
//   Queued
//     ↓ Tone3000Client::listModels(tone_id)
//   Listing
//     ↓ for each model: HTTP GET model_url
//   Downloading
//     ↓ atomic write bytes + sidecar JSON
//   Writing
//     ↓ LibraryDb::upsertModel + EventBus::post(ModelAdded)
//   Done
//
//   (any step can flip to Failed with an error_message.)
//
// Subscribers get a DownloadStatus snapshot on every stage / progress
// update. Subscriber callbacks fire on whichever thread published the
// event (usually the HTTP worker thread); UI subscribers must marshal
// onto the GUI thread themselves.
//
// Singleton — there's one global download queue per plugin instance.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Tone3000Types.h"
#include "../net/CancellationToken.h"

namespace t3k::cloud {

struct DownloadStatus {
  int id = 0;                              // download id (unique per session)
  int tone_id = 0;                         // TONE3000 tone id
  std::string tone_title;                  // human-readable label for the UI
  enum class Stage {
    Queued,
    Listing,
    Downloading,
    Writing,
    Done,
    Failed,
  };
  Stage stage = Stage::Queued;
  int   model_index = 0;                   // 0..total_models-1 — index of the model currently in flight
  int   total_models = 0;                  // 0 until Listing completes
  int64_t bytes_downloaded = 0;            // cumulative bytes for the current model
  std::string error_message;               // populated when stage == Failed
};

class Downloader {
public:
  static Downloader& instance();

  // Enqueue all of `t`'s models for download. Returns a per-session id
  // that callers can pass to cancel() or filter incoming status events.
  // Safe to call from any thread.
  int enqueueTone(const Tone& t);

  // Best-effort cancel — flips the underlying net::CancellationToken
  // and skips the remaining steps. Already-written files are left in
  // place; the LibraryDb row for those is upserted normally.
  void cancel(int id);

  using Listener = std::function<void(const DownloadStatus&)>;
  int  subscribe(Listener cb);
  void unsubscribe(int id);

  // Snapshot of every download still tracked (Done / Failed items
  // are pruned lazily on the next enqueueTone). Cheap copy.
  std::vector<DownloadStatus> active() const;

private:
  Downloader() = default;
  ~Downloader() = default;
  Downloader(const Downloader&) = delete;
  Downloader& operator=(const Downloader&) = delete;

  // Internal per-download bookkeeping. mItemMtx covers mItems +
  // mNextId. Status snapshots passed to listeners are copies.
  struct Item {
    DownloadStatus status;
    Tone           tone;                   // captured at enqueue time
    std::vector<Model> models;             // filled in Listing stage
    ::t3k::net::CancellationToken token;   // shared with in-flight HTTP req
  };

  // Pipeline stages. Each posts a status update before running.
  // All stages execute on the HTTP worker thread (HttpClient
  // completions chain into the next step). The shared_ptr<Item>
  // keeps the item alive across the async hops.
  void stepListing (std::shared_ptr<Item> it);
  void stepDownload(std::shared_ptr<Item> it, int modelIdx);
  void stepWrite   (std::shared_ptr<Item> it, int modelIdx,
                    const std::vector<uint8_t>& bytes);
  void finish      (std::shared_ptr<Item> it);
  void failItem    (std::shared_ptr<Item> it, std::string msg);

  // Snapshot the item's current status and fan out to subscribers.
  void publish(const DownloadStatus& s);

  mutable std::mutex                              mItemMtx;
  std::vector<std::shared_ptr<Item>>              mItems;
  std::atomic<int>                                mNextId{1};

  std::mutex                                      mListenerMtx;
  std::vector<std::pair<int, Listener>>           mListeners;
  std::atomic<int>                                mNextListenerId{1};
};

}  // namespace t3k::cloud
