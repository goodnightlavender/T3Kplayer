// EventBus.h — tiny synchronous listener registry.
//
// LibraryScanner publishes ModelAdded/Updated/Removed + Scan markers
// from its worker thread; LibraryView subscribes from the GUI thread to
// trigger refreshes. Listeners fire on the posting thread — the UI
// listener is responsible for marshaling onto the GUI thread (typically
// just SetDirty(false) inside the listener since iPlug2's redraw path
// is thread-safe-ish: SetDirty toggles a flag the paint thread reads).
//
// Singleton because the scanner is global and the UI is global; no
// scoping benefits from making it instantiable.

#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace t3k::library {

enum class LibraryEvent {
  ScanStarted,
  ModelAdded,
  ModelUpdated,
  ModelRemoved,
  ScanFinished,
};

class EventBus {
 public:
  using Listener = std::function<void(LibraryEvent, int64_t)>;

  static EventBus& instance();

  // Returns a non-zero subscription token. Listener is called for every
  // post() until unsubscribe() with this token. Safe to call from any
  // thread; the listener itself runs on whichever thread post() is
  // called on.
  int  subscribe(Listener cb);

  // Remove a subscription. No-op for unknown tokens.
  void unsubscribe(int id);

  // Fan out an event to all current listeners. Payload is event-typed:
  //   ModelAdded/Updated/Removed: row id of the affected model
  //   ScanStarted/ScanFinished:   reserved (unused; pass 0)
  void post(LibraryEvent ev, int64_t payload = 0);

 private:
  EventBus() = default;

  std::mutex                                 mMtx;
  std::vector<std::pair<int, Listener>>      mListeners;
  int                                        mNextId = 1;
};

}  // namespace t3k::library
