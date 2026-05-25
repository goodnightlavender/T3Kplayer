// T3kModelInfoPane — inspector-only model info pane (Phase 2b, Decision 46).
//
// Sits inside ToneView below the slot strip. Renders a 150×150 image on the
// left and title/meta/tag-chips/description on the right. NO action buttons,
// NO "stored offline" indicator (Decision 46 — Active/Replace are gone).
//
// If `imagePath` on the snapshot is non-empty, the bitmap is loaded via
// IGraphics::LoadBitmap on the next Draw. If the file is missing or load
// fails, the pane renders a placeholder gradient rounded rect (matches the
// v6 mockup's .plg-info-img fallback gradient).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "IControl.h"
#include "IGraphicsStructs.h"  // IBitmap
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::IBitmap;
using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;

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

class T3kModelInfoPane : public IControl
{
public:
  explicit T3kModelInfoPane(const IRECT& bounds);

  void setSnapshot(ModelInfoSnapshot s);
  void clear();   // empty-state copy: "Click + to load a model…"

  bool hasSnapshot() const { return mHasSnapshot; }

  void Draw(IGraphics& g) override;

private:
  ModelInfoSnapshot mSnapshot;
  bool mHasSnapshot = false;

  // Loaded once per imagePath change. IBitmap::IsValid() tells us whether
  // the load succeeded (placeholder is rendered otherwise).
  IBitmap mImage;
  std::string mLoadedImagePath;  // empty if mImage was never loaded

  // ThumbnailCache plumbing — when imagePath is empty but imageUrl is
  // set, kick a cache fetch on first Draw and pick up the local path
  // when the callback fires.
  bool        mThumbRequested = false;
  std::string mThumbForUrl;       // url we asked the cache for
  std::string mThumbPath;         // local path returned by the cache
  bool        mThumbLoadFailed = false;
};

}  // namespace t3k::ui
