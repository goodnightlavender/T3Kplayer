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

#include "IControl.h"
#include "IGraphicsStructs.h"  // for ISVG
#include "../theme.h"
#include "T3kGearIcon.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::ISVG;
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

  // Drag-to-reorder hooks. Only Loaded variants drag — Add tiles never do.
  // Called by the parent (ToneView) after construction; either may be null
  // (in which case drag is disabled even on Loaded tiles).
  //   onDragMove(slotIndex, mouseX, mouseY) — fired on every drag tick.
  //   onDragEnd (slotIndex, mouseX, mouseY) — fired on mouse-up after drag.
  void setOnDragMove(std::function<void(int, float, float)> cb) { mOnDragMove = std::move(cb); }
  void setOnDragEnd (std::function<void(int, float, float)> cb) { mOnDragEnd  = std::move(cb); }

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

  std::function<void(int)> mOnSelect;
  std::function<void(int)> mOnRemove;
  std::function<void(int)> mOnAdd;

  // Drag state. mDragging flips on after OnMouseDown moves a few pixels;
  // it stays true through OnMouseDrag and resets on OnMouseUp. The offset
  // tracks the cumulative drag delta so Draw can shift the visual to
  // follow the cursor without moving the actual mRECT (which keeps mouse
  // hit-testing predictable from the parent's perspective).
  bool  mDragging = false;
  float mDragOffsetX = 0.f;
  float mDragOffsetY = 0.f;

  std::function<void(int, float, float)> mOnDragMove;
  std::function<void(int, float, float)> mOnDragEnd;
};

}  // namespace t3k::ui
