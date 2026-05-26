// PresetStore.cpp — implementation. See PresetStore.h.
//
// All writes acquire LibraryDb::writeMutex() because we share the
// connection. Reads can race with writes safely thanks to
// SQLITE_OPEN_FULLMUTEX.

#include "PresetStore.h"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "LibraryDb.h"
#include "PresetState.h"

#include "nlohmann/json.hpp"
#include "sqlite3.h"

namespace t3k::library {

namespace {

using json = nlohmann::json;

int64_t NowMs()
{
  using clock = std::chrono::system_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             clock::now().time_since_epoch())
      .count();
}

void BindText(sqlite3_stmt* stmt, int idx, const std::string& s)
{
  sqlite3_bind_text(stmt, idx, s.data(),
                    static_cast<int>(s.size()), SQLITE_TRANSIENT);
}

std::string ColumnText(sqlite3_stmt* stmt, int col)
{
  const unsigned char* p = sqlite3_column_text(stmt, col);
  const int n = sqlite3_column_bytes(stmt, col);
  if (!p || n <= 0) return {};
  return std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(n));
}

// Convert a PresetState to its JSON dump. Stable field order makes the
// blob diff-friendly.
std::string SerializeState(const PresetState& s)
{
  json j;
  j["schema"] = PresetState::kSchemaVersion;

  json slots = json::array();
  for (const auto& slot : s.slots) {
    json o;
    o["index"]    = slot.slotIndex;
    o["tone_id"]  = slot.toneId.empty()  ? json(nullptr) : json(slot.toneId);
    o["model_id"] = slot.modelId.empty() ? json(nullptr) : json(slot.modelId);
    slots.push_back(std::move(o));
  }
  j["chain"]["slots"] = std::move(slots);

  j["master_output_db"] = s.master_output_db;

  j["knobs"]["input_db"]  = s.knobs.input_db;
  j["knobs"]["bass"]      = s.knobs.bass;
  j["knobs"]["mid"]       = s.knobs.mid;
  j["knobs"]["treble"]    = s.knobs.treble;
  j["knobs"]["output_db"] = s.knobs.output_db;
  return j.dump();
}

std::optional<PresetState> DeserializeState(const std::string& blob)
{
  if (blob.empty()) return std::nullopt;
  try {
    auto j = json::parse(blob);
    PresetState s;
    // Schema-1 blobs (Phase 2b's per-slot bypass/gain) are tolerated:
    // we read only the fields we still care about and drop the rest.
    if (j.contains("chain") && j["chain"].contains("slots")
        && j["chain"]["slots"].is_array()) {
      for (const auto& o : j["chain"]["slots"]) {
        PresetState::SlotEntry e;
        if (o.contains("index") && o["index"].is_number_integer()) {
          e.slotIndex = o["index"].get<int>();
        }
        if (o.contains("tone_id") && o["tone_id"].is_string()) {
          e.toneId = o["tone_id"].get<std::string>();
        }
        if (o.contains("model_id") && o["model_id"].is_string()) {
          e.modelId = o["model_id"].get<std::string>();
        }
        s.slots.push_back(std::move(e));
      }
    }
    if (j.contains("master_output_db") && j["master_output_db"].is_number()) {
      s.master_output_db = j["master_output_db"].get<double>();
    } else {
      s.master_output_db = 0.0;  // back-fill for older presets
    }
    if (j.contains("knobs") && j["knobs"].is_object()) {
      auto& k = j["knobs"];
      if (k.contains("input_db")  && k["input_db"].is_number())  s.knobs.input_db  = k["input_db"].get<float>();
      if (k.contains("bass")      && k["bass"].is_number())      s.knobs.bass      = k["bass"].get<float>();
      if (k.contains("mid")       && k["mid"].is_number())       s.knobs.mid       = k["mid"].get<float>();
      if (k.contains("treble")    && k["treble"].is_number())    s.knobs.treble    = k["treble"].get<float>();
      if (k.contains("output_db") && k["output_db"].is_number()) s.knobs.output_db = k["output_db"].get<float>();
    }
    return s;
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace

PresetStore& PresetStore::instance()
{
  static PresetStore p;
  return p;
}

PresetStore::PresetStore()
{
  ensureDefaults();
}

void PresetStore::ensureDefaults()
{
  sqlite3* db = LibraryDb::instance().raw();
  if (!db) return;
  std::lock_guard<std::mutex> lk(LibraryDb::instance().writeMutex());

  // Default Setting row. We don't ON CONFLICT here — we want to leave
  // the user's existing "Default Setting" alone if they renamed it
  // (we still need an active row regardless).
  sqlite3_stmt* exists = nullptr;
  bool defaultPresent = false;
  if (sqlite3_prepare_v2(db,
                         "SELECT 1 FROM presets WHERE id=1 LIMIT 1",
                         -1, &exists, nullptr) == SQLITE_OK) {
    defaultPresent = (sqlite3_step(exists) == SQLITE_ROW);
    sqlite3_finalize(exists);
  }
  if (!defaultPresent) {
    sqlite3_stmt* ins = nullptr;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO presets(id, name, state_json, created_at, updated_at) "
                           "VALUES (1, 'Default Setting', ?1, ?2, ?2)",
                           -1, &ins, nullptr) == SQLITE_OK) {
      const std::string blob = SerializeState(PresetState{});
      const int64_t now = NowMs();
      BindText(ins, 1, blob);
      sqlite3_bind_int64(ins, 2, now);
      sqlite3_step(ins);
      sqlite3_finalize(ins);
    }
  }

  // active_preset_id default → 1 (the Default Setting row).
  sqlite3_stmt* check = nullptr;
  bool activePresent = false;
  if (sqlite3_prepare_v2(db,
                         "SELECT v FROM meta_kv WHERE k='active_preset_id'",
                         -1, &check, nullptr) == SQLITE_OK) {
    activePresent = (sqlite3_step(check) == SQLITE_ROW);
    sqlite3_finalize(check);
  }
  if (!activePresent) {
    sqlite3_stmt* set = nullptr;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO meta_kv(k, v) VALUES('active_preset_id', '1') "
                           "ON CONFLICT(k) DO UPDATE SET v=excluded.v",
                           -1, &set, nullptr) == SQLITE_OK) {
      sqlite3_step(set);
      sqlite3_finalize(set);
    }
  }
}

std::vector<PresetStore::PresetRow> PresetStore::list()
{
  std::vector<PresetRow> out;
  sqlite3* db = LibraryDb::instance().raw();
  if (!db) return out;

  const int64_t active = activeId();

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db,
                         "SELECT id, name FROM presets ORDER BY name COLLATE NOCASE",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return out;
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    PresetRow r;
    r.id     = sqlite3_column_int64(stmt, 0);
    r.name   = ColumnText(stmt, 1);
    r.active = (r.id == active);
    out.push_back(std::move(r));
  }
  sqlite3_finalize(stmt);
  return out;
}

std::optional<PresetState> PresetStore::load(int64_t presetId)
{
  if (presetId <= 0) return std::nullopt;
  sqlite3* db = LibraryDb::instance().raw();
  if (!db) return std::nullopt;

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db,
                         "SELECT state_json FROM presets WHERE id=?1",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return std::nullopt;
  }
  sqlite3_bind_int64(stmt, 1, presetId);
  std::optional<PresetState> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    out = DeserializeState(ColumnText(stmt, 0));
  }
  sqlite3_finalize(stmt);
  return out;
}

int64_t PresetStore::saveCurrent(const PresetState& state)
{
  const int64_t id = activeId();
  if (id <= 0) return 0;

  sqlite3* db = LibraryDb::instance().raw();
  if (!db) return 0;
  std::lock_guard<std::mutex> lk(LibraryDb::instance().writeMutex());

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db,
                         "UPDATE presets SET state_json=?1, updated_at=?2, "
                         "sync_version=sync_version+1 WHERE id=?3",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return 0;
  }
  const std::string blob = SerializeState(state);
  BindText(stmt, 1, blob);
  sqlite3_bind_int64(stmt, 2, NowMs());
  sqlite3_bind_int64(stmt, 3, id);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return id;
}

int64_t PresetStore::saveAs(const std::string& name, const PresetState& state)
{
  if (name.empty()) return 0;

  sqlite3* db = LibraryDb::instance().raw();
  if (!db) return 0;
  std::lock_guard<std::mutex> lk(LibraryDb::instance().writeMutex());

  sqlite3_stmt* stmt = nullptr;
  // UPSERT by (case-insensitive) name. We don't ON CONFLICT against
  // the implicit unique index alone because we need the resulting id
  // back regardless of insert vs update.
  if (sqlite3_prepare_v2(db,
                         "INSERT INTO presets (name, state_json, created_at, updated_at) "
                         "VALUES (?1, ?2, ?3, ?3) "
                         "ON CONFLICT(name) DO UPDATE SET "
                         "  state_json=excluded.state_json, "
                         "  updated_at=excluded.updated_at, "
                         "  sync_version=sync_version+1",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return 0;
  }
  const std::string blob = SerializeState(state);
  const int64_t now = NowMs();
  BindText(stmt, 1, name);
  BindText(stmt, 2, blob);
  sqlite3_bind_int64(stmt, 3, now);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  // Resolve the id from the unique name.
  sqlite3_stmt* idStmt = nullptr;
  int64_t id = 0;
  if (sqlite3_prepare_v2(db,
                         "SELECT id FROM presets WHERE name=?1 COLLATE NOCASE",
                         -1, &idStmt, nullptr) == SQLITE_OK) {
    BindText(idStmt, 1, name);
    if (sqlite3_step(idStmt) == SQLITE_ROW) {
      id = sqlite3_column_int64(idStmt, 0);
    }
    sqlite3_finalize(idStmt);
  }
  return id;
}

void PresetStore::rename(int64_t presetId, const std::string& newName)
{
  if (presetId <= 0 || newName.empty()) return;
  sqlite3* db = LibraryDb::instance().raw();
  if (!db) return;
  std::lock_guard<std::mutex> lk(LibraryDb::instance().writeMutex());

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db,
                         "UPDATE presets SET name=?1, updated_at=?2, "
                         "sync_version=sync_version+1 WHERE id=?3",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return;
  }
  BindText(stmt, 1, newName);
  sqlite3_bind_int64(stmt, 2, NowMs());
  sqlite3_bind_int64(stmt, 3, presetId);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void PresetStore::remove(int64_t presetId)
{
  if (presetId <= 0) return;
  sqlite3* db = LibraryDb::instance().raw();
  if (!db) return;
  std::lock_guard<std::mutex> lk(LibraryDb::instance().writeMutex());

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "DELETE FROM presets WHERE id=?1",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return;
  }
  sqlite3_bind_int64(stmt, 1, presetId);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

int64_t PresetStore::activeId() const
{
  sqlite3* db = LibraryDb::instance().raw();
  if (!db) return 0;

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db,
                         "SELECT v FROM meta_kv WHERE k='active_preset_id'",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return 0;
  }
  int64_t out = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const std::string s = ColumnText(stmt, 0);
    try { out = static_cast<int64_t>(std::stoll(s)); } catch (...) { out = 0; }
  }
  sqlite3_finalize(stmt);
  return out;
}

void PresetStore::setActiveId(int64_t id)
{
  sqlite3* db = LibraryDb::instance().raw();
  if (!db) return;
  std::lock_guard<std::mutex> lk(LibraryDb::instance().writeMutex());

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db,
                         "INSERT INTO meta_kv(k, v) VALUES('active_preset_id', ?1) "
                         "ON CONFLICT(k) DO UPDATE SET v=excluded.v",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return;
  }
  const std::string s = std::to_string(id);
  BindText(stmt, 1, s);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

}  // namespace t3k::library
