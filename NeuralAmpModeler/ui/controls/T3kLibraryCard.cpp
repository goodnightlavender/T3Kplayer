// T3kLibraryCard.cpp — see T3kLibraryCard.h.

#include "T3kLibraryCard.h"

#include "IGraphics.h"

#include "../theme.h"
#include "../../config.h"  // ICON_*_FN

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

constexpr float kCardPad     = 12.f;
constexpr float kHeroH       = 96.f;
constexpr float kNameH       = 18.f;
constexpr float kMetaH       = 14.f;

// Map gear_type -> resource filename. Falls back to the pedal icon.
const char* GearIconFor(const std::string& gearType)
{
  if (gearType == "amp")       return ICON_AMP_FN;
  if (gearType == "cab")       return ICON_CAB_FN;
  if (gearType == "outboard")  return ICON_OUTBOARD_FN;
  if (gearType == "full-rig")  return ICON_FULLRIG_FN;
  return ICON_PEDAL_FN;
}

}  // namespace

T3kLibraryCard::T3kLibraryCard(const IRECT& bounds,
                               CardData data,
                               std::function<void(int64_t)> onClick,
                               std::function<void(int64_t, float, float)> onRightClick)
: IControl(bounds)
, mData(std::move(data))
, mOnClick(std::move(onClick))
, mOnRightClick(std::move(onRightClick))
{
  RecomputeRects();
}

void T3kLibraryCard::setData(CardData data)
{
  mData = std::move(data);
  SetDirty(false);
}

void T3kLibraryCard::setSelected(bool s)
{
  if (mSelected == s) return;
  mSelected = s;
  SetDirty(false);
}

void T3kLibraryCard::OnResize()
{
  RecomputeRects();
}

void T3kLibraryCard::RecomputeRects()
{
  const IRECT r = mRECT.GetPadded(-kCardPad);
  mHeroRect = IRECT(r.L, r.T, r.R, r.T + kHeroH);
  mNameRect = IRECT(r.L, mHeroRect.B + 6.f,
                    r.R, mHeroRect.B + 6.f + kNameH);
  mMetaRect = IRECT(r.L, mNameRect.B + 2.f,
                    r.R, mNameRect.B + 2.f + kMetaH);
}

void T3kLibraryCard::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  if (mod.R) {
    if (mOnRightClick) mOnRightClick(mData.id, x, y);
    return;
  }
  if (mOnClick) mOnClick(mData.id);
}

void T3kLibraryCard::OnMouseOver(float /*x*/, float /*y*/, const IMouseMod& /*mod*/)
{
  if (!mHovered) {
    mHovered = true;
    SetDirty(false);
  }
}

void T3kLibraryCard::OnMouseOut()
{
  if (mHovered) {
    mHovered = false;
    SetDirty(false);
  }
}

void T3kLibraryCard::OnMouseWheel(float /*x*/, float /*y*/,
                                  const IMouseMod& /*mod*/, float d)
{
  if (mOnWheel) mOnWheel(d);
}

void T3kLibraryCard::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Card surface — kBgSurface fill + border. Selected cards get the
  // accent ring; hovered ones get a slightly elevated fill (matches
  // T3kCard's hover cue).
  const IColor& fill = mHovered ? th::kBgElevated : th::kBgSurface;
  g.FillRoundRect(fill, mRECT, th::kRadiusMd);
  const IColor& outline = mSelected ? th::kBorderActive : th::kBorder;
  g.DrawRoundRect(outline, mRECT, th::kRadiusMd, nullptr, mSelected ? 2.f : 1.f);

  // Hero gear-icon tile.
  g.FillRoundRect(th::kBgBase, mHeroRect, th::kRadiusSm);
  g.DrawRoundRect(th::kBorder, mHeroRect, th::kRadiusSm, nullptr, 1.f);
  if (ISVG svg = g.LoadSVG(GearIconFor(mData.gearType)); svg.IsValid()) {
    const float inset = 18.f;
    g.DrawSVG(svg, mHeroRect.GetPadded(-inset));
  }

  // Display name — single line, clipped by the card padding rect.
  g.DrawText(IText(13.f, th::kText, th::kFontBodyMed,
                   EAlign::Near, EVAlign::Middle),
             mData.displayName.c_str(), mNameRect);

  // Meta: creator . format.
  std::string meta;
  if (!mData.creator.empty()) meta = mData.creator;
  if (!mData.format.empty()) {
    if (!meta.empty()) meta += " \xC2\xB7 ";
    meta += mData.format;
  }
  g.DrawText(IText(th::kTypeSmall, th::kTextMuted, th::kFontBody,
                   EAlign::Near, EVAlign::Middle),
             meta.c_str(), mMetaRect);
}

}  // namespace t3k::ui
