// Settings.cpp — JSON-backed persistent settings.
//
// Atomic-replace write strategy: serialize to a temp file in the same
// directory, then std::filesystem::rename onto the target. This avoids
// the half-written settings.json a crashing first-write would leave.

#include "Settings.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>

#include "../library/Paths.h"

// Single-header JSON via iPlug2's vendored copy
// (iPlug2/Dependencies/Extras/nlohmann/json.hpp — added to the include
// path by NeuralAmpModeler-win.props in Task 1).
#include "nlohmann/json.hpp"

namespace t3k::settings {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

// Read the whole file into a string. Returns false if the file doesn't
// exist OR can't be opened; we treat both as "first run" — the caller
// keeps the default-initialized Settings.
bool ReadFile(const fs::path& p, std::string& out)
{
  std::ifstream f(p, std::ios::in | std::ios::binary);
  if (!f.is_open()) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

// Parse a JSON blob into the Settings struct. Tolerant — missing fields
// keep their defaults; malformed JSON resets to defaults.
void ParseInto(const std::string& blob, Settings& s)
{
  try {
    auto j = json::parse(blob);
    if (j.contains("schema_version") && j["schema_version"].is_number_integer()) {
      s.schema_version = j["schema_version"].get<int>();
    }
    if (j.contains("tone3000_root") && j["tone3000_root"].is_string()) {
      s.tone3000_root = j["tone3000_root"].get<std::string>();
    }
    // 2026-05-25 — replaced the "window_size" string preset with a
    // float "window_scale". Read the new field if present.
    if (j.contains("window_scale") && j["window_scale"].is_number()) {
      s.window_scale = j["window_scale"].get<float>();
    }
    // Legacy migration: translate the old preset string to the new
    // float so users who saved with the previous schema land on a
    // sensible scale on first load. The next save() will write
    // window_scale and stop emitting window_size.
    else if (j.contains("window_size") && j["window_size"].is_string()) {
      const std::string ws = j["window_size"].get<std::string>();
      if      (ws == "medium") s.window_scale = 0.7f;
      else if (ws == "large")  s.window_scale = 0.875f;
      else                     s.window_scale = 1.35f;  // small or unknown
    }
    // Sanity-clamp out-of-range values (corrupted file, manual edit).
    if (s.window_scale < 0.4f || s.window_scale > 2.0f) {
      s.window_scale = 1.35f;
    }

    // 2026-05-25 — schema_version=4 migration: the design canvas was
    // trimmed (1100x688 -> 1024x640) and the default scale trimmed
    // (1.4 -> 1.35). Any file written under an earlier schema gets
    // its window_scale unconditionally reset to the new default so
    // the user sees the new layout immediately. The next save()
    // writes schema_version=4 (the Settings struct already
    // initializes it to 4), making the migration one-shot — schema-
    // 4 files honor whatever window_scale the user (or the corner-
    // resizer) wrote.
    const int loadedSchema =
        (j.contains("schema_version") && j["schema_version"].is_number_integer())
        ? j["schema_version"].get<int>()
        : 0;
    if (loadedSchema < 4) {
      s.window_scale = 1.35f;
    }
  } catch (...) {
    // Malformed file → keep defaults so the next save() restores a
    // clean version. The first-run modal will run again.
  }
}

// Singleton storage. Initialized on first instance() call.
struct Holder {
  Settings s;
  bool loaded = false;
  std::mutex mtx;
};

Holder& holder()
{
  static Holder h;
  return h;
}

void LoadFromDisk(Settings& s)
{
  ::t3k::library::Paths::ensureAppDataLayout();
  const std::string path = ::t3k::library::Paths::settingsPath();
  if (path.empty()) return;
  std::string blob;
  if (!ReadFile(fs::u8path(path), blob)) return;
  ParseInto(blob, s);
}

}  // namespace

Settings& instance()
{
  Holder& h = holder();
  std::lock_guard<std::mutex> lock(h.mtx);
  if (!h.loaded) {
    LoadFromDisk(h.s);
    h.loaded = true;
  }
  return h.s;
}

void save()
{
  Holder& h = holder();
  std::lock_guard<std::mutex> lock(h.mtx);

  const std::string path = ::t3k::library::Paths::settingsPath();
  if (path.empty()) return;

  ::t3k::library::Paths::ensureAppDataLayout();

  json j;
  j["schema_version"] = h.s.schema_version;
  j["tone3000_root"]  = h.s.tone3000_root;
  j["window_scale"]   = h.s.window_scale;

  // Pretty-print so the file is readable when the user pokes at it.
  const std::string blob = j.dump(2);

  // Atomic replace via temp file in the same directory.
  const fs::path target = fs::u8path(path);
  const fs::path tmp    = target.parent_path() / (target.filename().wstring() + L".tmp");

  std::error_code ec;
  {
    std::ofstream f(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return;
    f.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    if (!f) return;
  }
  fs::rename(tmp, target, ec);
  if (ec) {
    // rename() can fail on Windows if target exists — fall back to
    // remove + rename.
    fs::remove(target, ec);
    fs::rename(tmp, target, ec);
  }
}

std::mutex& mutex()
{
  return holder().mtx;
}

}  // namespace t3k::settings
