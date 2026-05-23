// ModelSidecar.cpp — implementation. See ModelSidecar.h.
//
// nlohmann/json is read inside a try/catch envelope; any deviation from
// the expected shape (wrong type, missing required keys) returns
// std::nullopt rather than asserting. The scanner logs the rejection
// and moves on so a malformed sidecar can't poison a whole walk.

#include "ModelSidecar.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

#include "nlohmann/json.hpp"

namespace t3k::library {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

// Lowercase ASCII (good enough for "nam" / "ir" extension matching).
std::string AsciiLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// Read the file at p into out. Returns false on any I/O error.
bool ReadFile(const fs::path& p, std::string& out)
{
  std::ifstream f(p, std::ios::in | std::ios::binary);
  if (!f.is_open()) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

// Convenience accessor: return obj[key] as a string when present and a
// string, else fallback.
std::string GetStr(const json& obj, const char* key,
                   const std::string& fallback = {})
{
  auto it = obj.find(key);
  if (it == obj.end() || !it->is_string()) return fallback;
  return it->get<std::string>();
}

// Returns the file's mtime in unix-ms, or 0 on failure.
//
// std::filesystem::file_time_type has an implementation-defined epoch.
// C++20 provides std::chrono::clock_cast for the conversion; pre-C++20
// the canonical fallback below works on MSVC: subtract the file_clock's
// "now" and add system_clock's "now" to land in the wall-clock domain.
int64_t StatMtimeMs(const fs::path& p)
{
  std::error_code ec;
  const auto t = fs::last_write_time(p, ec);
  if (ec) return 0;
  using namespace std::chrono;
  const auto sysNow  = system_clock::now();
  const auto fileNow = decltype(t)::clock::now();
  const auto delta   = t - fileNow;
  const auto sct =
      time_point_cast<system_clock::duration>(sysNow + delta);
  return duration_cast<milliseconds>(sct.time_since_epoch()).count();
}

int64_t StatSizeBytes(const fs::path& p)
{
  std::error_code ec;
  const auto n = fs::file_size(p, ec);
  if (ec) return 0;
  return static_cast<int64_t>(n);
}

}  // namespace

std::optional<ModelMeta> loadSidecarFor(const std::string& modelPath)
{
  if (modelPath.empty()) return std::nullopt;

  const fs::path mp = fs::u8path(modelPath);
  std::error_code ec;
  if (!fs::exists(mp, ec) || ec) return std::nullopt;

  // <stem>.tone3000.json next to the model file.
  fs::path sidecar = mp;
  sidecar.replace_extension();           // drop the .nam/.wav/...
  sidecar += ".tone3000.json";
  if (!fs::exists(sidecar, ec) || ec) return std::nullopt;

  std::string blob;
  if (!ReadFile(sidecar, blob)) return std::nullopt;

  json j;
  try {
    j = json::parse(blob);
  } catch (...) {
    return std::nullopt;
  }

  // Expected shape: { "schema": 1, "tone3000": { ... } }
  // Tolerate the missing wrapper by reading from the top-level too.
  const json* t = &j;
  auto wrap = j.find("tone3000");
  if (wrap != j.end() && wrap->is_object()) t = &(*wrap);

  ModelMeta m;
  m.uri        = mp.u8string();
  m.filename   = mp.filename().u8string();
  m.size_bytes = StatSizeBytes(mp);
  m.mtime      = StatMtimeMs(mp);

  m.t3k_tone_id  = GetStr(*t, "tone_id");
  m.t3k_model_id = GetStr(*t, "model_id");
  if (m.t3k_tone_id.empty() && m.t3k_model_id.empty()) {
    return std::nullopt;  // can't dedupe
  }

  m.display_name    = GetStr(*t, "tone_name");
  if (m.display_name.empty()) {
    m.display_name = mp.stem().u8string();
  }
  m.t3k_description = GetStr(*t, "description");
  m.t3k_image_url   = GetStr(*t, "image_url");
  m.gear_type       = GetStr(*t, "gear_type");
  m.make            = GetStr(*t, "make");
  m.model_name      = GetStr(*t, "model_name");

  // Creator can be either a nested object or a flat string for forward
  // compat with older sidecars.
  if (auto creator = t->find("creator"); creator != t->end()) {
    if (creator->is_object()) {
      m.t3k_creator    = GetStr(*creator, "username");
      m.t3k_creator_id = GetStr(*creator, "id");
    } else if (creator->is_string()) {
      m.t3k_creator = creator->get<std::string>();
    }
  }

  // Tags (best-effort; ignored if shape is wrong).
  if (auto tags = t->find("tags"); tags != t->end() && tags->is_array()) {
    for (const auto& tag : *tags) {
      if (tag.is_string()) m.tags.push_back(tag.get<std::string>());
    }
  }

  // Image sibling — only if the file actually exists on disk.
  if (const std::string imgName = GetStr(*t, "image_filename"); !imgName.empty()) {
    const fs::path imgPath = mp.parent_path() / fs::u8path(imgName);
    if (fs::exists(imgPath, ec) && !ec) {
      m.t3k_image_path = imgPath.u8string();
    }
  }

  // Kind from the file extension.
  const std::string ext = AsciiLower(mp.extension().u8string());
  if (ext == ".nam") {
    m.kind = "nam";
  } else if (ext == ".wav" || ext == ".flac" || ext == ".ogg") {
    m.kind = "ir";
  } else {
    m.kind = "nam";  // default
  }

  return m;
}

}  // namespace t3k::library
