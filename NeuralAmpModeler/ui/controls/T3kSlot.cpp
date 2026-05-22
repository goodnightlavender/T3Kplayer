// T3kSlot implementation. See T3kSlot.h.

#include "T3kSlot.h"

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// Hover-X visual constants. Sized to overlap the tile's top-right corner
// (Decision 45).
constexpr float kHoverXDiameter = 18.f;
constexpr float kHoverXInset    = 9.f;   // pulls the X half-out of the tile

// Inner padding around the gear icon — keeps the silhouette off the tile
// edges so the selection ring is legible.
constexpr float kIconInset = 8.f;

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

  // Loaded variant.
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

  // ── Hover-X (only when this tile is hovered) ────────────────────────
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

void T3kSlot::OnMouseDown(float x, float y, const IMouseMod& /*mod*/)
{
  if (mVariant == Variant::Add) {
    if (mOnAdd) mOnAdd(mSlotIndex);
    SetDirty(false);
    return;
  }

  // Loaded tile: click-on-X removes, click-elsewhere selects.
  if (mMouseIsOver && hoverXRect().Contains(x, y)) {
    if (mOnRemove) mOnRemove(mSlotIndex);
    SetDirty(false);
    return;
  }

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
