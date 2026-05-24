// Tone3000Types.h — strongly-typed mirrors of the TONE3000 public
// API objects from /api/v1/* (Phase 6).
//
// Field shapes and enum values are locked verbatim from the official
// SDK at https://github.com/tone-3000/api/blob/main/src/types.ts.
//
// Naming convention: the wire format is snake_case, the enum values
// are kebab-case strings ("full-rig", "best-match", etc.). C++ enums
// here use PascalCase identifiers and stringify via toWire() — we
// do NOT rely on stringification for round-tripping, so the
// identifier names don't have to match exactly.
//
// Parsing lives in Tone3000Client.cpp (json -> struct). The structs
// in this file are POD enough that copies are cheap; we move them
// into the completion callback.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace t3k::cloud {

// ── Enums ──────────────────────────────────────────────────────────

enum class Gear {
  Amp,        // "amp"
  FullRig,    // "full-rig"
  Pedal,      // "pedal"
  Outboard,   // "outboard"
  Ir,         // "ir"
};

enum class Platform {
  Nam,         // "nam"
  Ir,          // "ir"
  AidaX,       // "aida-x"
  AaSnapshot,  // "aa-snapshot"
  Proteus,     // "proteus"
};

enum class License {
  T3k,        // "t3k"
  CcBy,       // "cc-by"
  CcBySa,     // "cc-by-sa"
  CcByNc,     // "cc-by-nc"
  CcByNcSa,   // "cc-by-nc-sa"
  CcByNd,     // "cc-by-nd"
  CcByNcNd,   // "cc-by-nc-nd"
  Cco,        // "cco"
  Unknown,    // any string we don't recognize; not sent back to server
};

enum class Size {
  Standard,   // "standard"
  Lite,       // "lite"
  Feather,    // "feather"
  Nano,       // "nano"
  Custom,     // "custom"
};

enum class TonesSort {
  BestMatch,         // "best-match"
  Newest,            // "newest"
  Oldest,            // "oldest"
  Trending,          // "trending"
  DownloadsAllTime,  // "downloads-all-time"
};

// ── String <-> enum helpers ────────────────────────────────────────
// Defined inline so callers (UI sort-button, URL builder) don't need
// a separate .cpp link.

inline const char* toWire(Gear g) {
  switch (g) {
    case Gear::Amp:      return "amp";
    case Gear::FullRig:  return "full-rig";
    case Gear::Pedal:    return "pedal";
    case Gear::Outboard: return "outboard";
    case Gear::Ir:       return "ir";
  }
  return "amp";
}

inline std::optional<Gear> gearFromWire(const std::string& s) {
  if (s == "amp")      return Gear::Amp;
  if (s == "full-rig") return Gear::FullRig;
  if (s == "pedal")    return Gear::Pedal;
  if (s == "outboard") return Gear::Outboard;
  if (s == "ir")       return Gear::Ir;
  return std::nullopt;
}

inline const char* toWire(Platform p) {
  switch (p) {
    case Platform::Nam:        return "nam";
    case Platform::Ir:         return "ir";
    case Platform::AidaX:      return "aida-x";
    case Platform::AaSnapshot: return "aa-snapshot";
    case Platform::Proteus:    return "proteus";
  }
  return "nam";
}

inline std::optional<Platform> platformFromWire(const std::string& s) {
  if (s == "nam")         return Platform::Nam;
  if (s == "ir")          return Platform::Ir;
  if (s == "aida-x")      return Platform::AidaX;
  if (s == "aa-snapshot") return Platform::AaSnapshot;
  if (s == "proteus")     return Platform::Proteus;
  return std::nullopt;
}

inline License licenseFromWire(const std::string& s) {
  if (s == "t3k")          return License::T3k;
  if (s == "cc-by")        return License::CcBy;
  if (s == "cc-by-sa")     return License::CcBySa;
  if (s == "cc-by-nc")     return License::CcByNc;
  if (s == "cc-by-nc-sa")  return License::CcByNcSa;
  if (s == "cc-by-nd")     return License::CcByNd;
  if (s == "cc-by-nc-nd")  return License::CcByNcNd;
  if (s == "cco")          return License::Cco;
  return License::Unknown;
}

inline const char* toWire(Size sz) {
  switch (sz) {
    case Size::Standard: return "standard";
    case Size::Lite:     return "lite";
    case Size::Feather:  return "feather";
    case Size::Nano:     return "nano";
    case Size::Custom:   return "custom";
  }
  return "standard";
}

inline std::optional<Size> sizeFromWire(const std::string& s) {
  if (s == "standard") return Size::Standard;
  if (s == "lite")     return Size::Lite;
  if (s == "feather")  return Size::Feather;
  if (s == "nano")     return Size::Nano;
  if (s == "custom")   return Size::Custom;
  return std::nullopt;
}

inline const char* toWire(TonesSort s) {
  switch (s) {
    case TonesSort::BestMatch:        return "best-match";
    case TonesSort::Newest:           return "newest";
    case TonesSort::Oldest:           return "oldest";
    case TonesSort::Trending:         return "trending";
    case TonesSort::DownloadsAllTime: return "downloads-all-time";
  }
  return "best-match";
}

// Human-readable labels for UI controls (sort-cycle button, filter
// checkboxes). Distinct from toWire() so the wire format stays
// locked while we tweak copy.
inline const char* toLabel(Gear g) {
  switch (g) {
    case Gear::Amp:      return "Amp";
    case Gear::FullRig:  return "Full Rig";
    case Gear::Pedal:    return "Pedal";
    case Gear::Outboard: return "Outboard";
    case Gear::Ir:       return "IR";
  }
  return "Amp";
}

inline const char* toLabel(Size sz) {
  switch (sz) {
    case Size::Standard: return "Standard";
    case Size::Lite:     return "Lite";
    case Size::Feather:  return "Feather";
    case Size::Nano:     return "Nano";
    case Size::Custom:   return "Custom";
  }
  return "Standard";
}

inline const char* toLabel(TonesSort s) {
  switch (s) {
    case TonesSort::BestMatch:        return "Best match";
    case TonesSort::Newest:           return "Newest";
    case TonesSort::Oldest:           return "Oldest";
    case TonesSort::Trending:         return "Trending";
    case TonesSort::DownloadsAllTime: return "Most downloads";
  }
  return "Best match";
}

// ── Object schemas (mirrors of tone-3000/api/src/types.ts) ─────────

struct EmbeddedUser {
  std::string id;
  std::string username;
  std::optional<std::string> avatar_url;
  std::string url;
};

struct Make {
  std::optional<int> id;
  std::string name;
};

struct Tag {
  std::optional<int> id;
  std::string name;
};

struct Tone {
  int id = 0;
  std::string user_id;
  EmbeddedUser user;
  std::optional<std::string> created_at;
  std::optional<std::string> updated_at;
  std::string title;
  std::optional<std::string> description;
  Gear gear = Gear::Amp;
  std::optional<std::vector<std::string>> images;
  std::optional<bool> is_public;
  std::optional<std::vector<std::string>> links;
  Platform platform = Platform::Nam;
  License license = License::Unknown;
  std::vector<Size> sizes;
  std::vector<Make> makes;
  std::vector<Tag> tags;
  int models_count = 0;
  int downloads_count = 0;
  int favorites_count = 0;
  std::string url;
};

struct Model {
  int id = 0;
  std::string created_at;
  std::string updated_at;
  std::string user_id;
  std::string model_url;  // Bearer-gated; only valid for the signed-in user
  std::string name;
  Size size = Size::Standard;
  int tone_id = 0;
};

// ── Paginated envelope ─────────────────────────────────────────────

template <class T>
struct PaginatedResponse {
  std::vector<T> data;
  int page = 1;
  int page_size = 0;
  int total = 0;
  int total_pages = 0;

  bool hasNextPage() const { return page < total_pages; }
};

// ── Request param structs ──────────────────────────────────────────

struct SearchTonesParams {
  std::string query;
  int page = 1;
  int page_size = 25;  // API max
  TonesSort sort = TonesSort::BestMatch;
  std::vector<Gear> gears;
  std::vector<Size> sizes;
};

}  // namespace t3k::cloud
