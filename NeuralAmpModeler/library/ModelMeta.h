// ModelMeta.h — POD struct describing a single model file + sidecar.
//
// This is the dataflow between ModelSidecar (parses .tone3000.json) and
// LibraryDb (upserts into the `models` table). It is intentionally
// dependency-free (no sqlite, no nlohmann) so consumers can pass it
// around without including either header.
//
// Field naming matches the `models` table columns 1:1 plus a couple of
// scanner-only fields (`tags` doesn't live on `models`; it goes into
// model_tags via the scanner's tag-resolve step, which is Phase 3.5).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace t3k::library {

struct ModelMeta {
  // ── File-system facts (filled by the scanner) ─────────────────────
  std::string uri;            // absolute path
  std::string filename;       // basename of uri
  int64_t     size_bytes = 0;
  int64_t     mtime = 0;      // unix-ms

  // ── TONE3000 ids (required) ───────────────────────────────────────
  std::string t3k_tone_id;
  std::string t3k_model_id;

  // ── Display fields (from sidecar) ─────────────────────────────────
  std::string display_name;   // sidecar tone_name; falls back to file stem
  std::string t3k_creator;
  std::string t3k_creator_id;
  std::string t3k_description;
  std::string t3k_image_url;
  std::string t3k_image_path; // absolute, if image_filename sibling exists

  // ── Gear metadata (sidecar) ───────────────────────────────────────
  std::string gear_type;
  std::string make;
  std::string model_name;

  // ── Tag list (sidecar) ────────────────────────────────────────────
  // Stored on model_tags via the scanner — not on `models` itself.
  std::vector<std::string> tags;

  // ── Computed ──────────────────────────────────────────────────────
  std::string kind;           // "nam" or "ir" (from file extension)
};

}  // namespace t3k::library
