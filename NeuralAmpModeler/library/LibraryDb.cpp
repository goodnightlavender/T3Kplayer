// LibraryDb.cpp — implementation of the SQLite-backed library wrapper.
//
// All prepared statements are created lazily inside the method that
// needs them and finalized at the end of the call. The call sites are
// low-traffic (one upsert per scanned file; one query per Library tab
// keystroke), so re-prepare cost is negligible against an in-memory
// page cache.

#include "LibraryDb.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "ModelMeta.h"
#include "Paths.h"
#include "Schema.h"

#include "sqlite3.h"

namespace t3k::library {

namespace {

int64_t NowMs()
{
  using clock = std::chrono::system_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             clock::now().time_since_epoch())
      .count();
}

// Bind a TEXT parameter. Uses SQLITE_TRANSIENT so sqlite copies the
// string immediately — caller doesn't have to keep it alive.
void BindText(sqlite3_stmt* stmt, int idx, const std::string& s)
{
  sqlite3_bind_text(stmt, idx, s.data(),
                    static_cast<int>(s.size()), SQLITE_TRANSIENT);
}

void BindOrNullEmpty(sqlite3_stmt* stmt, int idx, const std::string& s)
{
  if (s.empty()) {
    sqlite3_bind_null(stmt, idx);
  } else {
    BindText(stmt, idx, s);
  }
}

std::string ColumnText(sqlite3_stmt* stmt, int col)
{
  const unsigned char* p = sqlite3_column_text(stmt, col);
  const int n = sqlite3_column_bytes(stmt, col);
  if (!p || n <= 0) return {};
  return std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(n));
}

std::optional<std::string> ColumnOptText(sqlite3_stmt* stmt, int col)
{
  if (sqlite3_column_type(stmt, col) == SQLITE_NULL) return std::nullopt;
  return ColumnText(stmt, col);
}

// Column list shared by every SELECT below. The HydrateModelRow indexes
// MUST match this order.
constexpr const char* kSelectColumns =
    "id, uri, filename, display_name, display_name_override, kind, "
    "size_bytes, mtime, added_at, missing, t3k_tone_id, t3k_model_id, "
    "t3k_creator, t3k_description, t3k_image_path, gear_type, make, "
    "model_name";

void HydrateModelRow(sqlite3_stmt* stmt, ModelRow& r)
{
  r.id                     = sqlite3_column_int64(stmt, 0);
  r.uri                    = ColumnText(stmt, 1);
  r.filename               = ColumnText(stmt, 2);
  r.display_name           = ColumnText(stmt, 3);
  r.display_name_override  = ColumnOptText(stmt, 4);
  r.kind                   = ColumnText(stmt, 5);
  r.size_bytes             = sqlite3_column_int64(stmt, 6);
  r.mtime                  = sqlite3_column_int64(stmt, 7);
  r.added_at               = sqlite3_column_int64(stmt, 8);
  r.missing                = sqlite3_column_int(stmt, 9) != 0;
  r.t3k_tone_id            = ColumnText(stmt, 10);
  r.t3k_model_id           = ColumnText(stmt, 11);
  r.t3k_creator            = ColumnText(stmt, 12);
  r.t3k_description        = ColumnText(stmt, 13);
  r.t3k_image_path         = ColumnOptText(stmt, 14);
  r.gear_type              = ColumnText(stmt, 15);
  r.make                   = ColumnText(stmt, 16);
  r.model_name             = ColumnText(stmt, 17);
}

}  // namespace

LibraryDb& LibraryDb::instance()
{
  static LibraryDb db;
  return db;
}

LibraryDb::LibraryDb()
{
  Paths::ensureAppDataLayout();
  const std::string path = Paths::libraryDbPath();
  if (path.empty()) return;

  if (sqlite3_open_v2(path.c_str(), &mDb,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    if (mDb) {
      sqlite3_close_v2(mDb);
      mDb = nullptr;
    }
    return;
  }

  // WAL + foreign keys. Errors here are not fatal — the schema migration
  // below will catch genuine breakage.
  sqlite3_exec(mDb, schema::kPragmaSetup, nullptr, nullptr, nullptr);

  migrate();
}

LibraryDb::~LibraryDb()
{
  if (mDb) {
    sqlite3_close_v2(mDb);
    mDb = nullptr;
  }
}

void LibraryDb::migrate()
{
  if (!mDb) return;
  std::lock_guard<std::mutex> lk(mWriteMtx);

  // Apply all DDL idempotently. CREATE IF NOT EXISTS makes this safe to
  // run on an existing DB. Phase 3 is schema 1.
  const char* steps[] = {
      schema::kCreateModels,
      schema::kCreateModelsIndexes,
      schema::kCreateTags,
      schema::kCreateModelTags,
      schema::kCreateFavorites,
      schema::kCreateRecents,
      schema::kCreateMetaKv,
      schema::kCreatePresets,
  };
  for (const char* sql : steps) {
    sqlite3_exec(mDb, sql, nullptr, nullptr, nullptr);
  }

  const int existing = readSchemaVersion();
  if (existing < schema::kCurrentVersion) {
    writeSchemaVersion(schema::kCurrentVersion);
  }
}

int LibraryDb::readSchemaVersion()
{
  if (!mDb) return 0;
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "SELECT v FROM meta_kv WHERE k='schema_version'";
  if (sqlite3_prepare_v2(mDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return 0;
  }
  int v = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const std::string s = ColumnText(stmt, 0);
    try { v = std::stoi(s); } catch (...) { v = 0; }
  }
  sqlite3_finalize(stmt);
  return v;
}

void LibraryDb::writeSchemaVersion(int v)
{
  if (!mDb) return;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO meta_kv(k, v) VALUES('schema_version', ?1) "
      "ON CONFLICT(k) DO UPDATE SET v=excluded.v";
  if (sqlite3_prepare_v2(mDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return;
  }
  const std::string s = std::to_string(v);
  BindText(stmt, 1, s);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

int64_t LibraryDb::upsertModel(const ModelMeta& m)
{
  if (!mDb || m.t3k_tone_id.empty() || m.t3k_model_id.empty()) return 0;
  std::lock_guard<std::mutex> lk(mWriteMtx);

  sqlite3_stmt* stmt = nullptr;
  const char* sql = R"SQL(
    INSERT INTO models (
      uri, filename, display_name, kind, size_bytes, mtime, added_at,
      missing, t3k_tone_id, t3k_model_id, t3k_creator, t3k_creator_id,
      t3k_description, t3k_image_url, t3k_image_path, gear_type, make,
      model_name, sync_version
    ) VALUES (
      ?1, ?2, ?3, ?4, ?5, ?6, ?7,
      0, ?8, ?9, ?10, ?11,
      ?12, ?13, ?14, ?15, ?16,
      ?17, 1
    )
    ON CONFLICT(t3k_tone_id, t3k_model_id) DO UPDATE SET
      uri             = excluded.uri,
      filename        = excluded.filename,
      display_name    = excluded.display_name,
      kind            = excluded.kind,
      size_bytes      = excluded.size_bytes,
      mtime           = excluded.mtime,
      missing         = 0,
      t3k_creator     = excluded.t3k_creator,
      t3k_creator_id  = excluded.t3k_creator_id,
      t3k_description = excluded.t3k_description,
      t3k_image_url   = excluded.t3k_image_url,
      t3k_image_path  = excluded.t3k_image_path,
      gear_type       = excluded.gear_type,
      make            = excluded.make,
      model_name      = excluded.model_name,
      sync_version    = sync_version + 1
  )SQL";
  if (sqlite3_prepare_v2(mDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return 0;
  }

  BindText (stmt, 1, m.uri);
  BindText (stmt, 2, m.filename);
  BindText (stmt, 3, m.display_name);
  BindText (stmt, 4, m.kind.empty() ? std::string("nam") : m.kind);
  sqlite3_bind_int64(stmt, 5, m.size_bytes);
  sqlite3_bind_int64(stmt, 6, m.mtime);
  sqlite3_bind_int64(stmt, 7, NowMs());
  BindText        (stmt,  8, m.t3k_tone_id);
  BindText        (stmt,  9, m.t3k_model_id);
  BindOrNullEmpty (stmt, 10, m.t3k_creator);
  BindOrNullEmpty (stmt, 11, m.t3k_creator_id);
  BindOrNullEmpty (stmt, 12, m.t3k_description);
  BindOrNullEmpty (stmt, 13, m.t3k_image_url);
  BindOrNullEmpty (stmt, 14, m.t3k_image_path);
  BindOrNullEmpty (stmt, 15, m.gear_type);
  BindOrNullEmpty (stmt, 16, m.make);
  BindOrNullEmpty (stmt, 17, m.model_name);

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) return 0;

  // Look up the id by the unique (tone_id, model_id) key — works for
  // both insert and update paths.
  sqlite3_stmt* idStmt = nullptr;
  const char* idSql =
      "SELECT id FROM models WHERE t3k_tone_id=?1 AND t3k_model_id=?2";
  if (sqlite3_prepare_v2(mDb, idSql, -1, &idStmt, nullptr) != SQLITE_OK) {
    if (idStmt) sqlite3_finalize(idStmt);
    return 0;
  }
  BindText(idStmt, 1, m.t3k_tone_id);
  BindText(idStmt, 2, m.t3k_model_id);
  int64_t out = 0;
  if (sqlite3_step(idStmt) == SQLITE_ROW) {
    out = sqlite3_column_int64(idStmt, 0);
  }
  sqlite3_finalize(idStmt);
  return out;
}

void LibraryDb::setDisplayNameOverride(int64_t id, std::optional<std::string> name)
{
  if (!mDb || id <= 0) return;
  std::lock_guard<std::mutex> lk(mWriteMtx);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "UPDATE models SET display_name_override=?1, "
      "sync_version=sync_version+1 WHERE id=?2";
  if (sqlite3_prepare_v2(mDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return;
  }
  if (name.has_value() && !name->empty()) {
    BindText(stmt, 1, *name);
  } else {
    sqlite3_bind_null(stmt, 1);
  }
  sqlite3_bind_int64(stmt, 2, id);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void LibraryDb::markMissing(int64_t id, bool missing)
{
  if (!mDb || id <= 0) return;
  std::lock_guard<std::mutex> lk(mWriteMtx);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "UPDATE models SET missing=?1, sync_version=sync_version+1 "
      "WHERE id=?2";
  if (sqlite3_prepare_v2(mDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return;
  }
  sqlite3_bind_int(stmt, 1, missing ? 1 : 0);
  sqlite3_bind_int64(stmt, 2, id);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

std::vector<ModelRow> LibraryDb::queryByName(const std::string& q, int limit)
{
  std::vector<ModelRow> out;
  if (!mDb) return out;

  sqlite3_stmt* stmt = nullptr;
  std::string sql;
  sql.reserve(512);
  sql += "SELECT ";
  sql += kSelectColumns;
  sql += " FROM models WHERE missing=0 ";

  const bool hasQuery = !q.empty();
  if (hasQuery) {
    sql +=
        "AND ("
        "  display_name LIKE ?1 ESCAPE '\\' COLLATE NOCASE "
        "  OR display_name_override LIKE ?1 ESCAPE '\\' COLLATE NOCASE "
        "  OR t3k_creator LIKE ?1 ESCAPE '\\' COLLATE NOCASE"
        ") ";
  }
  sql +=
      "ORDER BY COALESCE(display_name_override, display_name) COLLATE NOCASE "
      "LIMIT ";
  sql += hasQuery ? "?2" : "?1";

  if (sqlite3_prepare_v2(mDb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return out;
  }

  int limitIdx = 1;
  if (hasQuery) {
    // Escape SQL LIKE meta-characters in the user query, then wrap in
    // % for substring match. The ESCAPE '\\' clause in the SQL above
    // lets us pass literal % / _ through.
    std::string esc;
    esc.reserve(q.size() + 2);
    esc.push_back('%');
    for (char c : q) {
      if (c == '%' || c == '_' || c == '\\') esc.push_back('\\');
      esc.push_back(c);
    }
    esc.push_back('%');
    BindText(stmt, 1, esc);
    limitIdx = 2;
  }
  sqlite3_bind_int(stmt, limitIdx, limit > 0 ? limit : 200);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ModelRow r;
    HydrateModelRow(stmt, r);
    out.push_back(std::move(r));
  }
  sqlite3_finalize(stmt);
  return out;
}

std::optional<ModelRow> LibraryDb::findByToneAndModelId(const std::string& toneId,
                                                       const std::string& modelId)
{
  if (!mDb || toneId.empty() || modelId.empty()) return std::nullopt;
  sqlite3_stmt* stmt = nullptr;
  std::string sql = "SELECT ";
  sql += kSelectColumns;
  sql += " FROM models WHERE t3k_tone_id=?1 AND t3k_model_id=?2 LIMIT 1";

  if (sqlite3_prepare_v2(mDb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return std::nullopt;
  }
  BindText(stmt, 1, toneId);
  BindText(stmt, 2, modelId);

  std::optional<ModelRow> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    ModelRow r;
    HydrateModelRow(stmt, r);
    out = std::move(r);
  }
  sqlite3_finalize(stmt);
  return out;
}

void LibraryDb::markMissingExcept(const std::vector<ToneModelKey>& seen)
{
  if (!mDb) return;
  std::unordered_set<std::string> seenKeys;
  seenKeys.reserve(seen.size());
  for (const auto& k : seen) {
    seenKeys.insert(k.first + "\x1F" + k.second);  // unit-separator
  }

  std::lock_guard<std::mutex> lk(mWriteMtx);

  sqlite3_stmt* selStmt = nullptr;
  if (sqlite3_prepare_v2(mDb,
                         "SELECT id, t3k_tone_id, t3k_model_id "
                         "FROM models WHERE missing=0",
                         -1, &selStmt, nullptr) != SQLITE_OK) {
    if (selStmt) sqlite3_finalize(selStmt);
    return;
  }

  std::vector<int64_t> toMark;
  while (sqlite3_step(selStmt) == SQLITE_ROW) {
    const int64_t id   = sqlite3_column_int64(selStmt, 0);
    const std::string t = ColumnText(selStmt, 1);
    const std::string m = ColumnText(selStmt, 2);
    if (!seenKeys.count(t + "\x1F" + m)) {
      toMark.push_back(id);
    }
  }
  sqlite3_finalize(selStmt);

  if (toMark.empty()) return;

  sqlite3_stmt* upStmt = nullptr;
  if (sqlite3_prepare_v2(mDb,
                         "UPDATE models SET missing=1, "
                         "sync_version=sync_version+1 WHERE id=?1",
                         -1, &upStmt, nullptr) != SQLITE_OK) {
    if (upStmt) sqlite3_finalize(upStmt);
    return;
  }
  for (const int64_t id : toMark) {
    sqlite3_reset(upStmt);
    sqlite3_clear_bindings(upStmt);
    sqlite3_bind_int64(upStmt, 1, id);
    sqlite3_step(upStmt);
  }
  sqlite3_finalize(upStmt);
}

}  // namespace t3k::library
