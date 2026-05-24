// T3kSlot implementation. See T3kSlot.h.

#include "T3kSlot.h"

#include <algorithm>
#include <cmath>

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// Hover-X visual constants. Sized to overlap the tile's top-right corner
// (Decision 45).
constexpr float kHoverXDiameter = 22.f;
constexpr float kHoverXInset    = 11.f;  // pulls the X half-out of the tile

// Inner padding around the gear icon — keeps the silhouette off the tile
// edges so the selection ring is legible. Bumped to match the larger
// 88×88 tile (was 8 for the original 64×64).
constexpr float kIconInset = 12.f;

// Per-frame ease factor applied to the displayed drag offset. Higher = snappier;
// 0.4 gives a perceptibly weighted feel without lagging the cursor noticeably.
// When the residual is below this threshold the offset snaps to the target so
// we don't burn frames on imperceptible sub-pixel deltas.
constexpr float kDragSmoothFactor = 0.4f;
constexpr float kDragSnapEpsilon  = 0.3f;

}  // namespace

T3kSlot::T3kSlot(const IRECT& bounds,
                 int slotIndex,
                 Variant variant,
                 GearType iconType,
                 std::function<void(int)> onSelect,
                 std::function<void(int)> onRemove,
                 std::function<void(int)> onAdd)
: IControl(bounds)
, mSlotIndex(slotIndex)
, mVariant(variant)
, mIconType(iconType)
, mOnSelect(std::move(onSelect))
, mOnRemove(std::move(onRemove))
, mOnAdd(std::move(onAdd))
{
}

void T3kSlot::setIconType(GearType t)
{
  if (t == mIconType) return;
  mIconType = t;
  // Invalidate the cached SVG so the next Draw reloads the new file.
  mIconSvg.reset();
  SetDirty(false);
}

void T3kSlot::setSelected(bool s)
{
  if (s == mSelected) return;
  mSelected = s;
  SetDirty(false);
}

void T3kSlot::setVariant(Variant v)
{
  if (v == mVariant) return;
  mVariant = v;
  SetDirty(false);
}

IRECT T3kSlot::hoverXRect() const
{
  // 18×18 button anchored at the tile's top-right corner, half-overlapping
  // the edge so it reads as a separate affordance attached to the tile.
  const float cx = mRECT.R - kHoverXInset;
  const float cy = mRECT.T + kHoverXInset;
  const float r  = kHoverXDiameter * 0.5f;
  return IRECT(cx - r, cy - r, cx + r, cy + r);
}

void T3kSlot::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // ── Tile background + border ───────────────────────────────────────
  if (mVariant == Variant::Add) {
    // Dashed border + "+" glyph. iPlug2's NanoVG backend doesn't expose
    // a native dashed-stroke API at this revision, so render a solid
    // 1px border at the muted color. Reads as "placeholder" via the
    // glyph (the only tile with a "+" inside) and the lack of an icon.
    g.FillRoundRect(th::kBgSurface, mRECT, th::kRadiusLg);
    g.DrawRoundRect(th::kBorder, mRECT, th::kRadiusLg, nullptr, 1.f);

    const IText plus(20.f,
                     th::kTextMuted,
                     th::kFontBody,
                     EAlign::Center,
                     EVAlign::Middle);
    g.DrawText(plus, "+", mRECT);
    return;
  }

  // Loaded variant. When dragging, skip our own paint — T3kDragGhost
  // paints us at the offset position from the top of the strip's
  // z-order so the dragged tile renders above other slots. Without
  // this skip we'd double-render (once here at mRECT, once via the
  // ghost at the offset rect).
  if (mDragging) return;

  // Static paint at mRECT.
  g.FillRoundRect(th::kBgSurface, mRECT, th::kRadiusLg);

  // Selection ring: 1px kAccent border (replaces the default 1px subtle
  // border when selected). Decision 47.
  if (mSelected) {
    g.DrawRoundRect(th::kAccent, mRECT, th::kRadiusLg, nullptr, 1.f);
  } else {
    g.DrawRoundRect(th::kBorder, mRECT, th::kRadiusLg, nullptr, 1.f);
  }

  // ── Gear icon (inline draw — no child IControl) ─────────────────────
  if (!mIconSvg.has_value())
    mIconSvg.emplace(g.LoadSVG(T3kGearIcon::filenameFor(mIconType)));

  const IRECT iconRect = mRECT.GetPadded(-kIconInset);
  T3kGearIcon::drawInto(g, *mIconSvg, mIconType, iconRect);

  // ── Hover-X (only when hovered AND not dragging) ────────────────────
  if (mMouseIsOver) {
    const IRECT xr = hoverXRect();
    const float cx = xr.MW();
    const float cy = xr.MH();
    const float r  = xr.W() * 0.5f;

    g.FillCircle(th::kBgSurface, cx, cy, r);
    g.DrawCircle(th::kBorder, cx, cy, r, nullptr, 1.f);

    const IText xLabel(12.f,
                       th::kText,
                       th::kFontBodyBold,
                       EAlign::Center,
                       EVAlign::Middle);
    g.DrawText(xLabel, "\xC3\x97", xr);  // UTF-8 for ×
  }
}

void T3kSlot::drawAtDragOffset(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Ease the displayed offset toward the target. When the residual
  // is below the snap threshold, lock to the target so we don't burn
  // frames on imperceptible sub-pixel deltas.
  const float residual = mDragTargetX - mDragOffsetX;
  if (std::fabs(residual) > kDragSnapEpsilon) {
    mDragOffsetX += residual * kDragSmoothFactor;
  } else {
    mDragOffsetX = mDragTargetX;
  }

  const IRECT drawRect(mRECT.L + mDragOffsetX, mRECT.T,
                       mRECT.R + mDragOffsetX, mRECT.B);

  g.FillRoundRect(th::kBgSurface, drawRect, th::kRadiusLg);
  // Always accent-bordered while dragging — the user is actively
  // tracking this tile, and the accent edge reads as "lifted".
  g.DrawRoundRect(th::kAccent, drawRect, th::kRadiusLg, nullptr, 1.f);

  if (!mIconSvg.has_value())
    mIconSvg.emplace(g.LoadSVG(T3kGearIcon::filenameFor(mIconType)));

  const IRECT iconRect = drawRect.GetPadded(-kIconInset);
  T3kGearIcon::drawInto(g, *mIconSvg, mIconType, iconRect);
  // No hover-X while dragging — the user already committed to the
  // gesture; rendering a remove affordance under the cursor at the
  // same moment would be confusing.
}

bool T3kSlot::dragSmoothingActive() const
{
  if (!mDragging) return false;
  return std::fabs(mDragTargetX - mDragOffsetX) > kDragSnapEpsilon;
}

void T3kSlot::OnMouseDown(float x, float y, const IMouseMod& /*mod*/)
{
  if (mVariant == Variant::Add) {
    if (mOnAdd) mOnAdd(mSlotIndex);
    SetDirty(false);
    return;
  }

  // Loaded tile: click-on-X removes, click-elsewhere arms a potential drag.
  // The actual onSelect fires on OnMouseUp if no drag motion happened
  // (so a click still selects, but a click-and-drag is a reorder gesture).
  if (mMouseIsOver && hoverXRect().Contains(x, y)) {
    if (mOnRemove) mOnRemove(mSlotIndex);
    SetDirty(false);
    return;
  }

  // Reset drag offset. mDragging stays false until OnMouseDrag actually
  // fires — keeps simple clicks click-y.
  mDragTargetX = 0.f;
  mDragOffsetX = 0.f;
  SetDirty(false);
}

void T3kSlot::OnMouseDrag(float x, float y, float dX, float /*dY*/,
                          const IMouseMod& /*mod*/)
{
  // Add tiles never drag. Loaded tiles drag only when the parent wired up
  // mOnDragMove (categories that don't support reordering pass null).
  if (mVariant == Variant::Add) return;
  if (!mOnDragMove) return;

  // First drag tick: flip on mDragging and notify the parent so the
  // drag ghost can pick us up for top-of-z-order painting. iPlug2
  // only delivers OnMouseDrag after the cursor has actually moved
  // past its mouse-down position, so we don't need an explicit
  // motion threshold here. Drag is locked to the horizontal axis —
  // vertical motion is intentionally discarded.
  const bool wasDragging = mDragging;
  mDragging = true;
  if (!wasDragging && mOnDragStart) {
    mOnDragStart(mSlotIndex);
  }
  // Clamp the running drag offset against the category bounds set by
  // the parent. Without this, a tile can be visually dragged past the
  // amp/cab boundary or off the edge of the strip — the drop logic
  // would reject the move on mouse-up but the visual would mislead
  // the user during the gesture.
  mDragTargetX = std::min(std::max(mDragTargetX + dX, mDragMinOffsetX),
                          mDragMaxOffsetX);
  mOnDragMove(mSlotIndex, x, y);
  SetDirty(false);
}

void T3kSlot::OnMouseUp(float x, float y, const IMouseMod& /*mod*/)
{
  if (mVariant == Variant::Add) {
    return;  // OnMouseDown already fired the Add callback for that case.
  }

  if (mDragging) {
    // Drag-release: fire onDragEnd, then snap the visual back to mRECT
    // (the parent decides whether to rebuild the strip — if it does, this
    // tile gets destroyed anyway; if it doesn't, the visual just snaps).
    // We flip mDragging BEFORE firing onDragEnd so the ghost's clear()
    // (invoked by ToneView in the onDragEnd handler) sees a non-dragging
    // slot — otherwise the next paint cycle could re-render the ghost
    // until the source pointer was nulled.
    mDragging    = false;
    mDragTargetX = 0.f;
    mDragOffsetX = 0.f;
    if (mOnDragEnd) mOnDragEnd(mSlotIndex, x, y);
    SetDirty(false);
    return;
  }

  // No drag — treat as a click → select.
  if (mOnSelect) mOnSelect(mSlotIndex);
  SetDirty(false);
}

void T3kSlot::OnMouseOver(float x, float y, const IMouseMod& mod)
{
  IControl::OnMouseOver(x, y, mod);  // updates mMouseIsOver
  SetDirty(false);
}

void T3kSlot::OnMouseOut()
{
  IControl::OnMouseOut();
  SetDirty(false);
}

}  // namespace t3k::ui
