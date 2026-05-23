// LibraryDb.h — RAII wrapper around the local SQLite library database.
//
// Lives at `%LOCALAPPDATA%\TONE3000\library.db`. Opens with
// SQLITE_OPEN_FULLMUTEX so cross-thread reads from the UI thread + the
// LibraryScanner background thread are safe. Writes are additionally
// serialized through the public `writeMutex()` because SQLITE_OPEN_FULL-
// MUTEX guarantees per-statement atomicity but multi-statement
// transactions (the upsertModel path) need an external lock.
//
// Schema migration is driven by Schema.h's kCurrentVersion compared
// against meta_kv['schema_version']. Phase 3 only ever creates version 1.
//
// Singleton (Meyers). The first call to `instance()` constructs the
// connection and runs migrate(); subsequent calls return the same one.

#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;       // forward-decls so the sqlite header stays in the .cpp
struct sqlite3_stmt;

namespace t3k::library {

struct ModelMeta;     // defined in ModelMeta.h (Task 4)

// Row representation of one entry in the `models` table. Mirrors the
// schema 1:1; UI consumers (LibraryView, T3kCard) read from this struct
// directly — they don't talk to sqlite themselves.
struct ModelRow {
  int64_t                     id = 0;
  std::string                 uri;
  std::string                 filename;
  std::string                 display_name;
  std::optional<std::string>  display_name_override;
  std::string                 kind;          // "nam" or "ir"
  int64_t                     size_bytes = 0;
  int64_t                     mtime = 0;
  int64_t                     added_at = 0;
  bool                        missing = false;
  std::string                 t3k_tone_id;
  std::string                 t3k_model_id;
  std::string                 t3k_creator;
  std::string                 t3k_description;
  std::optional<std::string>  t3k_image_path;
  std::string                 gear_type;
  std::string                 make;
  std::string                 model_name;

  // Returns display_name_override if set, else display_name.
  const std::string& effectiveDisplayName() const {
    return display_name_override.has_value() ? *display_name_override
                                             : display_name;
  }
};

class LibraryDb {
 public:
  // Meyers singleton — never copied / moved.
  static LibraryDb& instance();

  // Insert-or-update the row identified by (t3k_tone_id, t3k_model_id).
  // Returns the row id of the upserted entry (0 on failure). Updates
  // mtime + missing=0 + all sidecar-driven columns; preserves
  // display_name_override.
  int64_t upsertModel(const ModelMeta& m);

  // Set or clear the user-rename column.
  void setDisplayNameOverride(int64_t id, std::optional<std::string> name);

  // missing=1 hides the row from the default LibraryView query but
  // keeps it around for redownload (Phase 7).
  void markMissing(int64_t id, bool missing);

  // Substring (case-insensitive) match on display_name OR
  // display_name_override OR t3k_creator. Empty query → all non-missing
  // rows ordered by effective display name.
  std::vector<ModelRow> queryByName(const std::string& q, int limit = 200);

  // Lookup by canonical TONE3000 ids. Used when Library row click →
  // load-into-slot resolves the URI to feed to the audio chain.
  std::optional<ModelRow> findByToneAndModelId(const std::string& toneId,
                                               const std::string& modelId);

  // Mark every row whose (tone_id, model_id) is NOT in the supplied
  // "seen" set as missing=1. Used by LibraryScanner at the end of a
  // full walk to detect deletions.
  using ToneModelKey = std::pair<std::string, std::string>;
  void markMissingExcept(const std::vector<ToneModelKey>& seen);

  // Direct connection access for PresetStore (it talks to the same
  // sqlite3* on the `presets` table). The mutex must be held by callers
  // during multi-statement work.
  sqlite3*    raw()        { return mDb; }
  std::mutex& writeMutex() { return mWriteMtx; }

 private:
  LibraryDb();
  ~LibraryDb();
  LibraryDb(const LibraryDb&) = delete;
  LibraryDb& operator=(const LibraryDb&) = delete;

  // Run schema migrations up to schema::kCurrentVersion. Idempotent.
  void migrate();

  // Read meta_kv['schema_version'] (default 0 if absent).
  int  readSchemaVersion();
  void writeSchemaVersion(int v);

  sqlite3*    mDb = nullptr;
  std::mutex  mWriteMtx;
};

}  // namespace t3k::library
