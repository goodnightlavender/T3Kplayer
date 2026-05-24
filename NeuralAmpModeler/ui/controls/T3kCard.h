// T3kCard — composite tone-row card (model preview tile).
//
// Visual layout (per .superpowers/brainstorm/.../mirror-cloud.html):
//   +------------------------------------------------------------+
//   | [ 88x88 ]   Title text                                     |
//   | [ image ]   Subtitle  [badge]                              |
//   | [   or   ]   v 12,345    * 42      # 8                     |
//   | [ ph.    ]   (18px avatar) creator · 1 week ago            |
//   +------------------------------------------------------------+
//   | > 0:00  ============================================ 0:28  |  (when selected)
//   +------------------------------------------------------------+
//
// Interaction:
//   - Click anywhere on the body  -> onSelect()
//   - Click the Download pill     -> onDownload()
//   - Hover                       -> mHoverAnim 0..1 (kAnimCardHover ms)
//   - setSelected(true) from the outside expands the player strip
//
// The player strip is drawn inline (T3kRainbowScrubber is NOT instantiated as
// a child) — for Phase 2 only the gradient track is shown; Phase 6 wires
// real audio behavior.

#pragma once

#include <functional>
#include <optional>
#include <string>

#include "IControl.h"
#include "IGraphicsStructs.h"  // IBitmap
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IBitmap;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::IMouseMod;

class T3kCard : public IControl
{
public:
  struct CardData {
    std::string title;
    std::string subtitle;
    std::string creator;
    std::string badgeText;
    // Direct bitmap (e.g. from a Library row that already loaded one).
    std::optional<IBitmap> image;
    // Remote thumbnail URL — when set, T3kCard asks
    // cloud::ThumbnailCache on first Draw and lazy-loads the cached
    // IBitmap once available. Ignored when `image` is already set.
    std::string imageUrl;
    int downloads = 0;
    int bookmarks = 0;
    int modelCount = 0;
    std::string relativeDate;
  };

  T3kCard(const IRECT& bounds,
          CardData data,
          std::function<void()> onSelect,
          std::function<void()> onDownload);

  // Wire a scroll-wheel forwarder. iPlug2 dispatches OnMouseWheel to
  // the topmost control under the cursor — without this hook, scroll
  // gestures over a card don't reach CloudView's scroll handler, and
  // the result list reads as unscrollable.
  void setOnWheel(std::function<void(float delta)> cb) { mOnWheel = std::move(cb); }

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const IMouseMod& mod) override;
  void OnMouseOut() override;
  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override;
  void OnResize() override;

  void setSelected(bool s) { mSelected = s; SetDirty(false); }
  bool isSelected() const { return mSelected; }

private:
  // Recompute the cached layout rects (image, right column, download pill,
  // player strip) given the current mRECT.
  void RecomputeRects();

  // Player strip height (only drawn when mSelected).
  static constexpr float kPlayerStripH = 28.f;

  CardData mData;
  std::function<void()> mOnSelect;
  std::function<void()> mOnDownload;
  std::function<void(float)> mOnWheel;  // wheel forwarder; nullable

  bool mSelected = false;
  bool mHovered  = false;
  float mHoverFrom = 0.f;
  float mHoverTo   = 0.f;

  // Lazy thumbnail wiring. On first Draw we ask cloud::ThumbnailCache
  // for mData.imageUrl; once the worker thread writes the file, the
  // callback stashes the local path here. The next paint cycle calls
  // g.LoadBitmap and we render the bitmap from then on.
  bool        mThumbRequested = false;
  std::string mThumbPath;          // populated when the cache resolves
  std::optional<IBitmap> mThumbBitmap;   // loaded lazily from mThumbPath
  bool        mThumbLoadFailed = false;  // gives up after one bad attempt

  // Cached rects (rebuilt in OnResize / RecomputeRects).
  IRECT mImageRect;
  IRECT mRightColRect;
  IRECT mDownloadRect;
  IRECT mPlayerStripRect;
};

}  // namespace t3k::ui
