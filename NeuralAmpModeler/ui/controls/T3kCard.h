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

  // Double-click handler — opens the tone detail page.
  void setOnDetail(std::function<void()> cb) { mOnDetail = std::move(cb); }

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const IMouseMod& mod) override;
  void OnMouseOut() override;
  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override;
  void OnResize() override;

  void setSelected(bool s) { mSelected = s; SetDirty(false); }
  bool isSelected() const { return mSelected; }

  // Per-card download status (Phase 7 polish). The Cloud-tab status
  // banner was getting occluded by cards that extend past the visible
  // bottom — so we surface the state directly on the DOWNLOAD pill of
  // the tone being downloaded.
  enum class DownloadState {
    Idle,    // default — pill renders as a 1px border with "DOWNLOAD"
    Active,  // queued / listing / downloading / writing
    Done,    // successfully landed in library
    Failed,  // pipeline failed; label carries the reason fragment
  };

  // Set state + override label in one call. Empty `label` keeps the
  // default label ("DOWNLOAD" for Idle, etc.). Marks the card dirty.
  void setDownloadState(DownloadState s, std::string label = {});
  DownloadState downloadState() const { return mDlState; }

  // Set the card's LOGICAL position — the un-clamped rect used for
  // content layout (image / title / pill positions). CloudView calls
  // this on every scroll tick alongside SetTargetAndDrawRECTs(clamped)
  // so the card's mRECT (used by iPlug2 to set the NanoVG scissor)
  // can stay inside the body while content stays at its true
  // scroll-position. This decoupling is what makes scroll clipping
  // work without distorting card contents at the top edge.
  void setLogicalRect(const IRECT& r);

private:
  // Recompute the cached layout rects (image, right column, download pill,
  // player strip) given the current mRECT.
  void RecomputeRects();

  // Player strip height — collapsed to 0 in the 2026-05-25 polish
  // round 3 so cloud cards no longer reserve an audio-preview strip
  // when selected. Kept as a named constant rather than removed so
  // RecomputeRects / Draw can stay structurally similar pending a
  // future scrubber design.
  static constexpr float kPlayerStripH = 0.f;

  CardData mData;
  std::function<void()> mOnSelect;
  std::function<void()> mOnDownload;
  std::function<void()> mOnDetail;       // dbl-click → detail page; nullable
  std::function<void(float)> mOnWheel;   // wheel forwarder; nullable

  bool mSelected = false;
  bool mHovered  = false;
  float mHoverFrom = 0.f;
  float mHoverTo   = 0.f;

  // Download status — drives the DOWNLOAD pill's appearance + text.
  DownloadState mDlState = DownloadState::Idle;
  std::string   mDlLabel;          // override; empty → state default

  // Lazy thumbnail wiring. On first Draw we ask cloud::ThumbnailCache
  // for mData.imageUrl; once the worker thread writes the file, the
  // callback stashes the local path here. The next paint cycle calls
  // g.LoadBitmap and we render the bitmap from then on.
  bool        mThumbRequested = false;
  std::string mThumbPath;          // populated when the cache resolves
  std::optional<IBitmap> mThumbBitmap;   // loaded lazily from mThumbPath
  bool        mThumbLoadFailed = false;  // gives up after one bad attempt

  // Logical layout rect — un-clamped by scroll clipping. Content
  // rects below are derived from this; iPlug2's mRECT (used for the
  // scissor) may be tighter.
  IRECT mLogicalRect;

  // Cached rects (rebuilt by RecomputeRects from mLogicalRect).
  IRECT mImageRect;
  IRECT mRightColRect;
  IRECT mDownloadRect;
  IRECT mPlayerStripRect;
};

}  // namespace t3k::ui
