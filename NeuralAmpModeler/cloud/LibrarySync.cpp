// LibrarySync.cpp — see LibrarySync.h.

#include "LibrarySync.h"

#include "Session.h"
#include "SessionEvent.h"
#include "SyncConfig.h"
#include "../library/EventBus.h"
#include "../library/LibraryDb.h"
#include "../library/ModelMeta.h"
#include "../net/HttpClient.h"
#include "../net/HttpRequest.h"
#include "../net/HttpResponse.h"

#include "nlohmann/json.hpp"

#include <sstream>

namespace t3k::cloud::sync {

namespace {

using json = nlohmann::json;

// Pull a string-or-null from a json object → std::string. Returns
// empty for missing / null / non-string fields so callers don't have
// to defend against the wire shape drifting.
std::string s(const json& j, const char* key)
{
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) return {};
  if (it->is_string()) return it->get<std::string>();
  return it->dump();
}

}  // namespace

LibrarySync& LibrarySync::instance()
{
  static LibrarySync inst;
  return inst;
}

std::string LibrarySync::entryUrl(const std::string& tone_id,
                                   const std::string& model_id) const
{
  return std::string(kLibrarySyncUrl)
       + "/v1/library/entry/" + tone_id + "/" + model_id;
}

std::string LibrarySync::entryJson(const ::t3k::library::ModelRow& row) const
{
  json j;
  j["tone_title"] = row.display_name;
  if (row.display_name_override.has_value()) {
    j["display_name_override"] = *row.display_name_override;
  }
  if (!row.t3k_creator.empty()) j["creator"]   = row.t3k_creator;
  if (!row.gear_type.empty())   j["gear_type"] = row.gear_type;
  // platform isn't stored on ModelRow; deferred. Worker tolerates
  // missing fields (NULL columns).
  // sync_version 0 → Worker treats this as "no prior knowledge" and
  // increments from its current value. First-cut LWW favors the
  // latest writer.
  j["sync_version"] = 0;
  return j.dump();
}

void LibrarySync::start()
{
  if (!isConfigured()) return;
  bool expected = false;
  if (!mSubscribed.compare_exchange_strong(expected, true)) return;
  mRunning.store(true, std::memory_order_release);

  // ── Session listener ──────────────────────────────────────────
  //   SignedIn               → kick a full pull
  //   SignedOut/SessionExp   → clear mRunning so in-flight pushes
  //                            don't apply their completions
  {
    std::lock_guard<std::mutex> lk(mMtx);
    mSessionListenerId = ::t3k::cloud::Session::instance().subscribe(
        [this](const ::t3k::cloud::SessionEvent& ev) {
          using K = ::t3k::cloud::SessionEvent::Kind;
          switch (ev.kind) {
            case K::SignedIn:
              this->mRunning.store(true, std::memory_order_release);
              this->pullLibrary();
              break;
            case K::SignedOut:
            case K::SessionExpired:
              this->mRunning.store(false, std::memory_order_release);
              break;
            default: break;
          }
        });
  }

  // ── EventBus listener ─────────────────────────────────────────
  // ModelAdded / ModelUpdated → look up the row by id, push it.
  {
    std::lock_guard<std::mutex> lk(mMtx);
    mEventBusListenerId = ::t3k::library::EventBus::instance().subscribe(
        [this](::t3k::library::LibraryEvent ev, int64_t payload) {
          if (ev != ::t3k::library::LibraryEvent::ModelAdded
              && ev != ::t3k::library::LibraryEvent::ModelUpdated) return;
          if (!this->mRunning.load(std::memory_order_acquire)) return;

          // LibraryDb doesn't expose a findById at this revision —
          // resolve via the empty-query path which returns every
          // non-missing row and filter. Cheap at typical library
          // sizes; Phase 9 polish can add a real lookup.
          const auto rows =
              ::t3k::library::LibraryDb::instance().queryByName("");
          for (const auto& r : rows) {
            if (r.id == payload) {
              this->pushEntry(r);
              break;
            }
          }
        });
  }

  // Immediate pull if we're already signed in (covers the case where
  // start() is called after sign-in already completed — typical at
  // plug-in launch when DPAPI restores a refresh token).
  if (::t3k::cloud::Session::instance().state()
      == ::t3k::cloud::Session::State::SignedIn) {
    this->pullLibrary();
  }
}

void LibrarySync::stop()
{
  bool expected = true;
  if (!mSubscribed.compare_exchange_strong(expected, false)) return;
  mRunning.store(false, std::memory_order_release);

  std::lock_guard<std::mutex> lk(mMtx);
  if (mSessionListenerId > 0) {
    ::t3k::cloud::Session::instance().unsubscribe(mSessionListenerId);
    mSessionListenerId = 0;
  }
  if (mEventBusListenerId > 0) {
    ::t3k::library::EventBus::instance().unsubscribe(mEventBusListenerId);
    mEventBusListenerId = 0;
  }
}

void LibrarySync::pushEntry(const ::t3k::library::ModelRow& row)
{
  if (!isConfigured()) return;
  if (!mRunning.load(std::memory_order_acquire)) return;
  auto token = ::t3k::cloud::Session::instance().accessTokenIfValid();
  if (!token.has_value()) return;
  if (row.t3k_tone_id.empty() || row.t3k_model_id.empty()) return;

  ::t3k::net::HttpRequest req;
  req.method  = ::t3k::net::HttpMethod::Put;
  req.url     = entryUrl(row.t3k_tone_id, row.t3k_model_id);
  req.headers["Authorization"] = "Bearer " + *token;
  req.headers["Content-Type"]  = "application/json";
  req.headers["Accept"]        = "application/json";
  req.headers["User-Agent"]    = "TONE3000Player/0.1";

  const std::string body = entryJson(row);
  req.body.assign(body.begin(), body.end());
  req.timeout_ms = 15'000;

  ::t3k::net::HttpClient::instance().send(std::move(req),
      [](const ::t3k::net::HttpResponse& /*res*/) {
        // No UI surface for sync errors yet. Silent failure is
        // acceptable for first cut — the row is still on local disk
        // and LibraryDb; the user just doesn't get cross-device sync
        // for this write. Phase 9 polish can add a status toast.
      });
}

void LibrarySync::pullLibrary(PullCompletion onDone)
{
  if (!isConfigured()) {
    if (onDone) onDone(false, 0);
    return;
  }
  auto token = ::t3k::cloud::Session::instance().accessTokenIfValid();
  if (!token.has_value()) {
    if (onDone) onDone(false, 0);
    return;
  }

  ::t3k::net::HttpRequest req;
  req.method  = ::t3k::net::HttpMethod::Get;
  req.url     = std::string(kLibrarySyncUrl) + "/v1/library";
  req.headers["Authorization"] = "Bearer " + *token;
  req.headers["Accept"]        = "application/json";
  req.headers["User-Agent"]    = "TONE3000Player/0.1";
  req.timeout_ms = 30'000;

  ::t3k::net::HttpClient::instance().send(std::move(req),
      [onDone = std::move(onDone)](const ::t3k::net::HttpResponse& res) {
        if (res.status_code < 200 || res.status_code >= 300
            || res.body.empty()) {
          if (onDone) onDone(false, 0);
          return;
        }

        int applied = 0;
        try {
          const std::string body(res.body.begin(), res.body.end());
          const auto j = json::parse(body);
          const auto it = j.find("entries");
          if (it == j.end() || !it->is_array()) {
            if (onDone) onDone(false, 0);
            return;
          }
          for (const auto& e : *it) {
            if (!e.is_object()) continue;
            const std::string tone_id  = s(e, "tone_id");
            const std::string model_id = s(e, "model_id");
            if (tone_id.empty() || model_id.empty()) continue;

            // Upsert into LibraryDb as a "known-from-server-but-not-
            // on-disk-yet" row. We synthesize a `uri = sync://<…>`
            // stub so it doesn't collide with real disk paths;
            // markMissing(true) keeps the row hidden from the
            // default Library view until the user re-downloads.
            // Phase 9's restore-modal will resolve these stubs by
            // re-running the Phase 7 download flow.
            ::t3k::library::ModelMeta meta;
            meta.uri             = "sync://" + tone_id + "/" + model_id;
            meta.filename        = model_id;
            meta.t3k_tone_id     = tone_id;
            meta.t3k_model_id    = model_id;
            meta.display_name    = s(e, "tone_title");
            if (meta.display_name.empty()) meta.display_name = model_id;
            meta.t3k_creator     = s(e, "creator");
            meta.gear_type       = s(e, "gear_type");
            meta.t3k_image_url   = s(e, "image_url");
            meta.kind            = "nam";  // best guess; corrected on
                                           // real download.

            const int64_t rowId =
                ::t3k::library::LibraryDb::instance().upsertModel(meta);
            if (rowId > 0) {
              ::t3k::library::LibraryDb::instance().markMissing(rowId, true);
              // Apply display_name_override if the server has one.
              if (auto ov = e.find("display_name_override");
                  ov != e.end() && ov->is_string()) {
                const std::string sv = ov->get<std::string>();
                if (!sv.empty()) {
                  ::t3k::library::LibraryDb::instance()
                      .setDisplayNameOverride(rowId, sv);
                }
              }
              ::t3k::library::EventBus::instance().post(
                  ::t3k::library::LibraryEvent::ModelAdded, rowId);
              ++applied;
            }
          }
        } catch (...) {
          if (onDone) onDone(false, 0);
          return;
        }
        if (onDone) onDone(true, applied);
      });
}

}  // namespace t3k::cloud::sync
