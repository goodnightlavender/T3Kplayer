// ModelInfoSnapshot — value type describing a loaded model's metadata.
//
// Originally lived inside T3kModelInfoPane.h, but the pane control was
// retired with the v6 redesign. The struct is still consumed by
// ChainView::LoadedSlot and T3kFocusedSlot, so it moved here to make
// its role (a plain value type, not an IControl) explicit.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace t3k::ui {

struct ModelInfoSnapshot {
  std::string imagePath;       // absolute path; preferred
  std::string imageUrl;        // remote URL — resolved via
                               // cloud::ThumbnailCache when imagePath
                               // is empty.
  std::string displayName;     // e.g. "Klon '94 — original-circuit centaur clone"
  std::string creator;
  std::string format;          // "NAM" or "IR"
  int64_t     sizeBytes = 0;
  int64_t     downloadedAtMs = 0;
  std::vector<std::string> tags;
  std::string description;
};

}  // namespace t3k::ui
