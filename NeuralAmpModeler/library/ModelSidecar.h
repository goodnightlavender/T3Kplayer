// ModelSidecar.h — tolerant reader for `<stem>.tone3000.json` files.
//
// Returns std::nullopt only when the file fundamentally isn't a
// TONE3000 sidecar (file missing, unparseable JSON, no tone_id +
// model_id). Every other field is best-effort with empty-string
// fallbacks; the upsert into LibraryDb tolerates NULLs across the
// optional columns.

#pragma once

#include <optional>
#include <string>

#include "ModelMeta.h"

namespace t3k::library {

// Resolves the sidecar path from the model file (foo.nam -> foo.tone3000.json),
// stats the model file (size + mtime), parses the JSON, and returns the
// populated ModelMeta. `image_filename` from the sidecar is resolved
// against the model's parent directory; if that sibling exists on disk,
// `t3k_image_path` is set to the absolute path.
//
// Returns std::nullopt when:
//   - the model file doesn't exist
//   - the sidecar doesn't exist
//   - the sidecar can't be parsed
//   - the sidecar lacks both tone_id AND model_id (we can't dedupe it)
std::optional<ModelMeta> loadSidecarFor(const std::string& modelPath);

// Write a `<modelPath-stem>.tone3000.json` sibling alongside `modelPath`
// containing the canonical TONE3000 fields from `m`. The format mirrors
// what loadSidecarFor reads (wrapper key "tone3000" with snake_case
// fields inside).
//
// Returns false on any I/O / serialization failure. The write is
// atomic: bytes hit `<sidecar>.partial` first, then we rename onto
// the final path so a mid-write crash never leaves a half-parsed
// sidecar.
bool writeSidecarFor(const std::string& modelPath, const ModelMeta& m);

}  // namespace t3k::library
