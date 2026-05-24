// T3kSlot — signal-chain slot tile. Icon-only; no LED, no bypass state, no
// label (Decisions 44, 47, mockup v6).
//
// A tile holds a single T3kGearIcon child sized to fill most of the inner
// area. Two variants:
//
//   - Loaded : draws the icon. On hover, reveals an 18×18 "×" button in
//              the top-right corner (Decision 45). Click the X to remove
//              the model from this chain position; click anywhere else to
//              select the slot (the info pane below the strip updates).
//   - Add    : the trailing "+" placeholder. Dashed border, "+" glyph in
//              kTextMuted, no icon, no hover-X. Click opens the model
//              picker.
//
// Tile dimensions are caller-controlled (the strip sizes them):
//   - 64×64 for non-FullRig loaded slots
//   - 82×64 for FullRig (wider, to fit the stacked amp+cab silhouette)
//   - 44×64 for the Add variant
//
// Selection is rendered as a 1px kAccent border + 1px inset shadow. T3kSlot
// itself is purely visual; chain state (which slot is selected, which slot
// indices are occupied) lives in ToneView and is pushed in via setters.

#pragma once

#include <functional>
#include <optional>
#include <string>

#include "IControl.h"
#include "IGraphicsStructs.h"  // for ISVG, IBitmap
#include "../theme.h"
#include "T3kGearIcon.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::ISVG;
using ::iplug::igraphics::IBitmap;
using ::iplug::igraphics::IMouseMod;

class T3kSlot : public IControl
{
public:
  enum class Variant { Loaded, Add };

  T3kSlot(const IRECT& bounds,
          int slotIndex,
          Variant variant,
          GearType iconType,
          std::function<void(int)> onSelect,
          std::function<void(int)> onRemove,
          std::function<void(int)> onAdd);

  // Mutators used by the parent (ToneView) to update the tile without
  // re-creating the control.
  void setIconType(GearType t);
  void setSelected(bool s);
  void setVariant(Variant v);
  // Set the slot's image — when imagePath resolves to a loadable
  // bitmap, the tile renders the image (fit-cover, rounded) instead
  // of the gear-type SVG. Empty path falls back to the icon. imageUrl
  // is an optional remote source resolved via cloud::ThumbnailCache
  // when imagePath is empty.
  void setImage(std::string imagePath, std::string imageUrl);

  // Drag-to-reorder hooks. Only Loaded variants drag — Add tiles never do.
  // Called by the parent (ToneView) after construction; any may be null
  // (onDragMove being null disables drag entirely — OnMouseDrag bails
  // early in that case).
  //   onDragStart(slotIndex) — fired on the FIRST drag tick (false→true
  //                            transition of mDragging). ToneView points
  //                            T3kDragGhost at this slot so the drag
  //                            visual renders above other tiles.
  //   onDragMove(slotIndex, mouseX, mouseY) — fired on every drag tick.
  //   onDragEnd (slotIndex, mouseX, mouseY) — fired on mouse-up after drag.
  void setOnDragStart(std::function<void(int)> cb)               { mOnDragStart = std::move(cb); }
  void setOnDragMove (std::function<void(int, float, float)> cb) { mOnDragMove  = std::move(cb); }
  void setOnDragEnd  (std::function<void(int, float, float)> cb) { mOnDragEnd   = std::move(cb); }

  // Clamp the dragged tile's horizontal offset to keep it within its
  // category. ToneView computes (minOffsetX, maxOffsetX) relative to
  // mRECT.L so the drawn tile cannot cross the amp/cab boundary or run
  // off the strip's edge. Both default to a wide range (effectively no
  // clamp) until the parent sets them.
  //   minOffsetX: most-negative drag offset allowed (tile slid LEFT)
  //   maxOffsetX: most-positive drag offset allowed (tile slid RIGHT)
  void setDragBoundsX(float minOffsetX, float maxOffsetX) {
    mDragMinOffsetX = minOffsetX;
    mDragMaxOffsetX = maxOffsetX;
  }

  // Paint the tile at its CURRENT dragged offset position. Called by
  // T3kDragGhost during the drag so the moving tile renders above its
  // siblings regardless of attach-order z-order. Includes the smoothing
  // tick (mDragOffsetX eases toward mDragTargetX) so calling this once
  // per frame keeps the ease alive.
  void drawAtDragOffset(IGraphics& g);

  // True iff the ease residual between target and displayed offset is
  // still visible. T3kDragGhost reads this to decide whether to keep
  // marking itself dirty.
  bool dragSmoothingActive() const;

  // True between OnMouseDown and OnMouseUp while the user is dragging a
  // Loaded tile. Read by ToneView's drop-indicator paint, if any.
  bool isDragging() const { return mDragging; }

  Variant variant()  const { return mVariant; }
  bool    selected() const { return mSelected; }

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override;
  void OnMouseUp(float x, float y, const IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const IMouseMod& mod) override;
  void OnMouseOut() override;

private:
  // The 18×18 hover-X rect, positioned at the top-right corner of mRECT.
  // Computed lazily because mRECT moves when the parent resizes.
  IRECT hoverXRect() const;

  int      mSlotIndex;
  Variant  mVariant;
  bool     mSelected = false;
  GearType mIconType;

  // The gear-icon SVG is drawn inline (no child IControl). Lazy-loaded on
  // first Draw and invalidated when mIconType changes — same pattern as
  // T3kLogo. ISVG has no default ctor, hence the optional.
  std::optional<ISVG> mIconSvg;

  // Image rendering (Phase-10 polish 2026-05-25). When the slot has
  // an image (local file path, or a URL that ThumbnailCache resolves),
  // render the bitmap fit-cover inside the tile and skip the gear
  // icon. Falls back to the icon on missing/corrupt images.
  std::string mImagePath;
  std::string mImageUrl;
  bool        mBitmapLoaded     = false;
  bool        mBitmapLoadFailed = false;
  IBitmap     mBitmap;
  bool        mThumbRequested   = false;
  std::string mThumbPath;
  bool        mThumbLoadFailed  = false;

  std::function<void(int)> mOnSelect;
  std::function<void(int)> mOnRemove;
  std::function<void(int)> mOnAdd;

  // Drag state. mDragging flips on the first OnMouseDrag tick and resets
  // on OnMouseUp. Drag is constrained to the horizontal axis (pedals /
  // outboards reorder within their category in 1D), and the displayed
  // offset eases toward the target each frame for smoothing.
  //   mDragTargetX — raw accumulated horizontal drag delta (1:1 with the
  //                  mouse, clamped to [mDragMinOffsetX, mDragMaxOffsetX]).
  //   mDragOffsetX — what's actually rendered; lerped toward mDragTargetX
  //                  in drawAtDragOffset. Decoupling target from displayed
  //                  value gives the drag a smooth, weighted feel.
  bool  mDragging    = false;
  float mDragTargetX = 0.f;
  float mDragOffsetX = 0.f;

  // X-axis drag clamp. Defaults to a wide-enough range that
  // unconfigured callers get unconstrained drag; ToneView sets real
  // bounds on every rebuildStrip() so the dragged tile can't escape
  // its category.
  float mDragMinOffsetX = -1e6f;
  float mDragMaxOffsetX =  1e6f;

  std::function<void(int)>               mOnDragStart;
  std::function<void(int, float, float)> mOnDragMove;
  std::function<void(int, float, float)> mOnDragEnd;
};

}  // namespace t3k::ui
