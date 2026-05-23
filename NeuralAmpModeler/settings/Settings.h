// Settings.h — JSON-backed persistent settings for TONE3000 Player.
//
// Backs `%LOCALAPPDATA%\TONE3000\settings.json`. Schema is intentionally
// tiny in Phase 3: just the chosen tone3000_root (empty until the user
// runs the first-run modal). Future toggles append into the same struct
// and gain a defaulted accessor — Settings::load() tolerates missing
// fields so older saved files keep working.
//
// Single-instance Meyers singleton. `instance()` loads from disk on the
// first call; subsequent calls return the cached struct.
//
// Thread-safety: the singleton is the read path only — UI reads the
// values from any thread. Writes go through save() which serializes via
// the singleton's internal mutex.

#pragma once

#include <mutex>
#include <string>

namespace t3k::settings {

struct Settings {
  int         schema_version = 1;
  std::string tone3000_root;  // empty => first run
};

// Returns the singleton settings struct. Mutable reference — callers
// edit fields in place then call save() to persist. Loaded on first
// call (may create the AppData layout via Paths::ensureAppDataLayout).
Settings& instance();

// Atomic write: serializes the current settings struct to a temp file
// then ReplaceFile()s onto settings.json. No-op (logged) on failure.
void save();

// Mutex guarding instance() / save(). Exposed because the first-run
// modal modifies the struct and saves in sequence and we want the
// read-modify-write to be atomic from the caller's perspective.
std::mutex& mutex();

}  // namespace t3k::settings
