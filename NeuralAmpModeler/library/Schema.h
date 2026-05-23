// Schema.h — SQL DDL strings used by LibraryDb::migrate().
//
// Lifted verbatim from `docs/superpowers/specs/2026-05-21-tone3000-nam-fork-design.md`
// §8 "Local library subsystem · SQLite schema". When the spec changes,
// bump kCurrentVersion and add a new migration arm in LibraryDb.cpp —
// do NOT edit prior versions' strings (they're the source of truth for
// older library.db files in the wild).
//
// Schema version 1 (Phase 3): models + tags + model_tags + favorites +
// recents + meta_kv + presets.

#pragma once

namespace t3k::library {

namespace schema {

inline constexpr int kCurrentVersion = 1;

// ── Tables ─────────────────────────────────────────────────────────────

inline constexpr const char* kCreateModels = R"SQL(
CREATE TABLE IF NOT EXISTS models (
  id                    INTEGER PRIMARY KEY,
  uri                   TEXT NOT NULL UNIQUE,
  filename              TEXT NOT NULL,
  display_name          TEXT NOT NULL,
  display_name_override TEXT,
  kind                  TEXT NOT NULL,
  size_bytes            INTEGER NOT NULL,
  mtime                 INTEGER NOT NULL,
  added_at              INTEGER NOT NULL,
  missing               INTEGER NOT NULL DEFAULT 0,
  t3k_tone_id           TEXT NOT NULL,
  t3k_model_id          TEXT NOT NULL,
  t3k_creator           TEXT,
  t3k_creator_id        TEXT,
  t3k_description       TEXT,
  t3k_image_url         TEXT,
  t3k_image_path        TEXT,
  gear_type             TEXT,
  make                  TEXT,
  model_name            TEXT,
  source                TEXT NOT NULL DEFAULT 'tone3000',
  sync_version          INTEGER NOT NULL DEFAULT 0,
  synced_at             INTEGER
);
)SQL";

inline constexpr const char* kCreateModelsIndexes = R"SQL(
CREATE INDEX IF NOT EXISTS idx_models_kind         ON models(kind);
CREATE INDEX IF NOT EXISTS idx_models_gear_type    ON models(gear_type);
CREATE INDEX IF NOT EXISTS idx_models_display_name ON models(display_name COLLATE NOCASE);
CREATE UNIQUE INDEX IF NOT EXISTS idx_models_t3k_ids ON models(t3k_tone_id, t3k_model_id);
)SQL";

inline constexpr const char* kCreateTags = R"SQL(
CREATE TABLE IF NOT EXISTS tags (
  id     INTEGER PRIMARY KEY,
  name   TEXT NOT NULL UNIQUE COLLATE NOCASE,
  source TEXT NOT NULL
);
)SQL";

inline constexpr const char* kCreateModelTags = R"SQL(
CREATE TABLE IF NOT EXISTS model_tags (
  model_id INTEGER NOT NULL REFERENCES models(id) ON DELETE CASCADE,
  tag_id   INTEGER NOT NULL REFERENCES tags(id)   ON DELETE CASCADE,
  PRIMARY KEY (model_id, tag_id)
);
)SQL";

inline constexpr const char* kCreateFavorites = R"SQL(
CREATE TABLE IF NOT EXISTS favorites (
  model_id INTEGER PRIMARY KEY REFERENCES models(id) ON DELETE CASCADE,
  added_at INTEGER NOT NULL
);
)SQL";

inline constexpr const char* kCreateRecents = R"SQL(
CREATE TABLE IF NOT EXISTS recents (
  model_id INTEGER PRIMARY KEY REFERENCES models(id) ON DELETE CASCADE,
  used_at  INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_recents_used_at ON recents(used_at DESC);
)SQL";

inline constexpr const char* kCreateMetaKv = R"SQL(
CREATE TABLE IF NOT EXISTS meta_kv (
  k TEXT PRIMARY KEY,
  v TEXT NOT NULL
);
)SQL";

inline constexpr const char* kCreatePresets = R"SQL(
CREATE TABLE IF NOT EXISTS presets (
  id            INTEGER PRIMARY KEY,
  name          TEXT    NOT NULL UNIQUE COLLATE NOCASE,
  state_json    TEXT    NOT NULL,
  created_at    INTEGER NOT NULL,
  updated_at    INTEGER NOT NULL,
  sync_version  INTEGER NOT NULL DEFAULT 0,
  synced_at     INTEGER
);
CREATE INDEX IF NOT EXISTS idx_presets_name ON presets(name COLLATE NOCASE);
)SQL";

// PRAGMAs applied at open time. WAL gives us concurrent reader while
// the scanner thread writes. Foreign keys are off by default in SQLite
// and we want them.
inline constexpr const char* kPragmaSetup =
  "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;";

}  // namespace schema

}  // namespace t3k::library
