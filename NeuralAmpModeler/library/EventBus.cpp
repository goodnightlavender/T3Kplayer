// EventBus.cpp — listener registry implementation. See EventBus.h.

#include "EventBus.h"

namespace t3k::library {

EventBus& EventBus::instance()
{
  static EventBus bus;
  return bus;
}

int EventBus::subscribe(Listener cb)
{
  std::lock_guard<std::mutex> lk(mMtx);
  const int id = mNextId++;
  mListeners.emplace_back(id, std::move(cb));
  return id;
}

void EventBus::unsubscribe(int id)
{
  if (id <= 0) return;
  std::lock_guard<std::mutex> lk(mMtx);
  for (auto it = mListeners.begin(); it != mListeners.end(); ++it) {
    if (it->first == id) {
      mListeners.erase(it);
      return;
    }
  }
}

void EventBus::post(LibraryEvent ev, int64_t payload)
{
  // Copy under the lock so listeners can't deadlock by subscribing /
  // unsubscribing inside their own callback (a single nested
  // unsubscribe is a common reset path).
  std::vector<Listener> snapshot;
  {
    std::lock_guard<std::mutex> lk(mMtx);
    snapshot.reserve(mListeners.size());
    for (const auto& p : mListeners) snapshot.push_back(p.second);
  }
  for (const auto& l : snapshot) {
    if (l) l(ev, payload);
  }
}

}  // namespace t3k::library
