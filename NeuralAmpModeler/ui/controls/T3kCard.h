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
    std::optional<IBitmap> image;
    int downloads = 0;
    int bookmarks = 0;
    int modelCount = 0;
    std::string relativeDate;
  };

  T3kCard(const IRECT& bounds,
          CardData data,
          std::function<void()> onSelect,
          std::function<void()> onDownload);

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const IMouseMod& mod) override;
  void OnMouseOut() override;
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

  bool mSelected = false;
  bool mHovered  = false;
  float mHoverFrom = 0.f;
  float mHoverTo   = 0.f;

  // Cached rects (rebuilt in OnResize / RecomputeRects).
  IRECT mImageRect;
  IRECT mRightColRect;
  IRECT mDownloadRect;
  IRECT mPlayerStripRect;
};

}  // namespace t3k::ui
