// LibrarySync.cpp — see LibrarySync.h.

#include "LibrarySync.h"

#include "Session.h"
#include "SessionEvent.h"
#include "SyncConfig.h"
#include "Tone3000Client.h"
#include "Downloader.h"
#include "../library/EventBus.h"
#include "../library/LibraryDb.h"
#include "../library/ModelMeta.h"
#include "../net/HttpClient.h"
#include "../net/HttpRequest.h"
#include "../net/HttpResponse.h"

#include "nlohmann/json.hpp"
#include "sqlite3.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>

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

std::string ColumnText(sqlite3_stmt* stmt, int col)
{
  const unsigned char* p = sqlite3_column_text(stmt, col);
  const int n = sqlite3_column_bytes(stmt, col);
  if (!p || n <= 0) return {};
  return std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(n));
}

int64_t NowMs()
{
  using clock = std::chrono::system_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             clock::now().time_since_epoch()).count();
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
  if (!row.t3k_description.empty()) j["description"] = row.t3k_description;
  if (!row.model_name.empty())      j["model_name"]  = row.model_name;
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
              this->pullPresets();
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
    this->pullPresets();
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

void LibrarySync::pushPreset(int64_t presetId)
{
  if (!isConfigured() || presetId <= 1) return;
  if (!mRunning.load(std::memory_order_acquire)) return;
  auto token = ::t3k::cloud::Session::instance().accessTokenIfValid();
  if (!token.has_value()) return;

  sqlite3* db = ::t3k::library::LibraryDb::instance().raw();
  if (!db) return;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db,
                         "SELECT id, name, state_json, sort_order FROM presets WHERE id=?1",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return;
  }
  sqlite3_bind_int64(stmt, 1, presetId);
  if (sqlite3_step(stmt) != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return;
  }

  json j;
  const auto id = sqlite3_column_int64(stmt, 0);
  j["name"] = ColumnText(stmt, 1);
  j["state_json"] = ColumnText(stmt, 2);
  j["sort_order"] = sqlite3_column_int(stmt, 3);
  j["sync_version"] = 0;
  sqlite3_finalize(stmt);

  ::t3k::net::HttpRequest req;
  req.method = ::t3k::net::HttpMethod::Put;
  req.url = std::string(kLibrarySyncUrl) + "/v1/presets/" + std::to_string(id);
  req.headers["Authorization"] = "Bearer " + *token;
  req.headers["Content-Type"] = "application/json";
  req.headers["Accept"] = "application/json";
  req.headers["User-Agent"] = "T3KPlayer/0.1";
  const std::string body = j.dump();
  req.body.assign(body.begin(), body.end());
  req.timeout_ms = 15'000;
  ::t3k::net::HttpClient::instance().send(std::move(req), [](const auto&) {});
}

void LibrarySync::deletePreset(int64_t presetId)
{
  if (!isConfigured() || presetId <= 1) return;
  if (!mRunning.load(std::memory_order_acquire)) return;
  auto token = ::t3k::cloud::Session::instance().accessTokenIfValid();
  if (!token.has_value()) return;

  ::t3k::net::HttpRequest req;
  req.method = ::t3k::net::HttpMethod::Delete;
  req.url = std::string(kLibrarySyncUrl) + "/v1/presets/" + std::to_string(presetId);
  req.headers["Authorization"] = "Bearer " + *token;
  req.headers["Accept"] = "application/json";
  req.headers["User-Agent"] = "T3KPlayer/0.1";
  req.timeout_ms = 15'000;
  ::t3k::net::HttpClient::instance().send(std::move(req), [](const auto&) {});
}

void LibrarySync::pullPresets()
{
  if (!isConfigured()) return;
  auto token = ::t3k::cloud::Session::instance().accessTokenIfValid();
  if (!token.has_value()) return;

  ::t3k::net::HttpRequest req;
  req.method = ::t3k::net::HttpMethod::Get;
  req.url = std::string(kLibrarySyncUrl) + "/v1/presets";
  req.headers["Authorization"] = "Bearer " + *token;
  req.headers["Accept"] = "application/json";
  req.headers["User-Agent"] = "T3KPlayer/0.1";
  req.timeout_ms = 30'000;

  ::t3k::net::HttpClient::instance().send(std::move(req),
      [](const ::t3k::net::HttpResponse& res) {
        if (res.status_code < 200 || res.status_code >= 300 || res.body.empty()) return;
        try {
          const auto j = json::parse(std::string(res.body.begin(), res.body.end()));
          const auto it = j.find("presets");
          if (it == j.end() || !it->is_array()) return;
          sqlite3* db = ::t3k::library::LibraryDb::instance().raw();
          if (!db) return;
          std::lock_guard<std::mutex> lk(::t3k::library::LibraryDb::instance().writeMutex());
          for (const auto& p : *it) {
            const int64_t id = std::stoll(s(p, "preset_id"));
            if (id <= 1) continue;
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db,
                "INSERT INTO presets(id, name, state_json, created_at, updated_at, sort_order) "
                "VALUES(?1, ?2, ?3, ?4, ?4, ?5) "
                "ON CONFLICT(id) DO UPDATE SET name=excluded.name, "
                "state_json=excluded.state_json, updated_at=excluded.updated_at, "
                "sort_order=excluded.sort_order",
                -1, &stmt, nullptr) != SQLITE_OK) {
              if (stmt) sqlite3_finalize(stmt);
              continue;
            }
            const auto now = NowMs();
            const std::string name = s(p, "name");
            const std::string state = s(p, "state_json");
            sqlite3_bind_int64(stmt, 1, id);
            sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, state.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 4, now);
            sqlite3_bind_int(stmt, 5, p.value("sort_order", 0));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
          }
        } catch (...) {
        }
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
      [this, onDone = std::move(onDone)](const ::t3k::net::HttpResponse& res) {
        // Fan out the result to onDone AND any persistent listener
        // registered via setPullListener. The listener pattern lets
        // ToneRoot react to every pull (e.g. show the restore modal)
        // without each call site having to wire its own completion.
        auto fireListeners = [this](bool ok, int entries) {
          PullListener listener;
          {
            std::lock_guard<std::mutex> lk(this->mMtx);
            listener = this->mPullListener;
          }
          if (listener) listener(ok, entries);
        };

        if (res.status_code < 200 || res.status_code >= 300
            || res.body.empty()) {
          if (onDone) onDone(false, 0);
          fireListeners(false, 0);
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

            // 2026-05-25 — guard against clobbering on-disk records.
            // upsertModel's ON CONFLICT clause overwrites the URI to
            // the supplied value AND resets missing=0; we then call
            // markMissing(true) right after. For a row the
            // LibraryScanner has already discovered on disk (its
            // `uri` is a real file path, e.g.
            // `C:\...\<toneId>\<modelId>.nam`), this would replace
            // its URI with a `sync://` stub and flip it to missing=1
            // — making the LibraryView act as if the user has nothing
            // downloaded AND making the restore-library modal pop up
            // every launch (countLocalMissing() returns the entire
            // server library). Skip those rows entirely; only upsert
            // genuinely-missing (server-known, not-on-disk) entries.
            if (auto existing =
                    ::t3k::library::LibraryDb::instance()
                        .findByToneAndModelId(tone_id, model_id);
                existing.has_value()
                && !existing->uri.empty()
                && existing->uri.rfind("sync://", 0) != 0) {
              if (auto ov = e.find("display_name_override");
                  ov != e.end() && ov->is_string()) {
                ::t3k::library::LibraryDb::instance()
                    .setDisplayNameOverride(existing->id, ov->get<std::string>());
              }
              const std::string desc = s(e, "description");
              if (!desc.empty()) {
                ::t3k::library::LibraryDb::instance()
                    .setDescription(existing->id, desc);
              }
              continue;
            }

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
            meta.t3k_description = s(e, "description");
            meta.gear_type       = s(e, "gear_type");
            meta.model_name      = s(e, "model_name");
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
          fireListeners(false, 0);
          return;
        }
        if (onDone) onDone(true, applied);
        fireListeners(true, applied);
      });
}

void LibrarySync::setPullListener(PullListener cb)
{
  std::lock_guard<std::mutex> lk(mMtx);
  mPullListener = std::move(cb);
}

int LibrarySync::countLocalMissing() const
{
  // queryByName("") with the default limit returns up to 200 non-
  // missing rows by design — it filters missing=1 OUT. We need the
  // count of MISSING rows so we'd ordinarily add a dedicated query.
  // For Phase 8 polish we lean on LibraryDb's raw connection: a
  // single COUNT(*) prepared statement. Caller is on the GUI thread.
  sqlite3* db = ::t3k::library::LibraryDb::instance().raw();
  if (!db) return 0;
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "SELECT COUNT(*) FROM models WHERE missing = 1";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }
  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return count;
}

void LibrarySync::restoreAllMissing(RestoreCompletion onDone)
{
  if (!isConfigured()) {
    if (onDone) onDone(0, 0);
    return;
  }
  if (!::t3k::cloud::Session::instance().accessTokenIfValid().has_value()) {
    if (onDone) onDone(0, 0);
    return;
  }

  // Walk LibraryDb rows where missing=1 and dedupe to unique tone_ids.
  // We need raw SQL here for the missing=1 filter — queryByName hides
  // missing rows by design.
  std::vector<std::string> tone_ids;
  {
    sqlite3* db = ::t3k::library::LibraryDb::instance().raw();
    if (!db) {
      if (onDone) onDone(0, 0);
      return;
    }
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT DISTINCT t3k_tone_id FROM models "
        "WHERE missing = 1 AND t3k_tone_id <> '' "
        "ORDER BY t3k_tone_id";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      if (onDone) onDone(0, 0);
      return;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const unsigned char* p = sqlite3_column_text(stmt, 0);
      if (p) tone_ids.emplace_back(reinterpret_cast<const char*>(p));
    }
    sqlite3_finalize(stmt);
  }

  if (tone_ids.empty()) {
    if (onDone) onDone(0, 0);
    return;
  }

  // Shared progress counters — getTone fires async, so we tally as
  // each response lands. shared_ptr keeps the state alive across
  // worker-thread callbacks.
  struct Progress {
    std::atomic<int> queued{0};
    std::atomic<int> failed{0};
    std::atomic<int> pending{0};
    RestoreCompletion onDone;
  };
  auto prog = std::make_shared<Progress>();
  prog->pending.store(static_cast<int>(tone_ids.size()),
                      std::memory_order_release);
  prog->onDone = std::move(onDone);

  for (const auto& tone_id_str : tone_ids) {
    int tone_id = 0;
    try {
      tone_id = std::stoi(tone_id_str);
    } catch (...) {
      tone_id = 0;
    }
    if (tone_id <= 0) {
      prog->failed.fetch_add(1, std::memory_order_relaxed);
      if (prog->pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        if (prog->onDone) prog->onDone(prog->queued.load(),
                                       prog->failed.load());
      }
      continue;
    }

    ::t3k::cloud::Tone3000Client::instance().getTone(
        tone_id,
        [prog](::t3k::cloud::ToneResult r) {
          if (r.success) {
            ::t3k::cloud::Downloader::instance().enqueueTone(r.data);
            prog->queued.fetch_add(1, std::memory_order_relaxed);
          } else {
            prog->failed.fetch_add(1, std::memory_order_relaxed);
          }
          if (prog->pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            if (prog->onDone) prog->onDone(prog->queued.load(),
                                           prog->failed.load());
          }
        });
  }
}

}  // namespace t3k::cloud::sync
