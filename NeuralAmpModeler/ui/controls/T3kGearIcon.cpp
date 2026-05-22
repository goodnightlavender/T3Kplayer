// T3kGearIcon implementation. See T3kGearIcon.h.

#include "T3kGearIcon.h"

#include "IGraphics.h"
#include "../../config.h"  // ICON_{PEDAL,AMP,CAB,OUTBOARD,FULLRIG}_FN

namespace t3k::ui {

using namespace iplug::igraphics;

const char* T3kGearIcon::filenameFor(GearType t)
{
  switch (t) {
    case GearType::Pedal:    return ICON_PEDAL_FN;
    case GearType::Amp:      return ICON_AMP_FN;
    case GearType::Cab:      return ICON_CAB_FN;
    case GearType::Outboard: return ICON_OUTBOARD_FN;
    case GearType::FullRig:  return ICON_FULLRIG_FN;
  }
  return ICON_PEDAL_FN;  // unreachable; fallback to pedal
}

float T3kGearIcon::aspectFor(GearType t)
{
  // Source SVG viewBox aspect ratios (width/height). Used to letterbox the
  // icon inside its bounds so the silhouette never distorts when the
  // parent slot resizes.
  switch (t) {
    case GearType::Pedal:    return 40.f / 46.f;  // tall pedal
    case GearType::Amp:      return 50.f / 24.f;  // wide amp head
    case GearType::Cab:      return 56.f / 36.f;  // wide cab
    case GearType::Outboard: return 56.f / 32.f;  // wide rack unit
    case GearType::FullRig:  return 56.f / 52.f;  // squarish
  }
  return 1.f;
}

void T3kGearIcon::drawInto(IGraphics& g, ISVG& svg, GearType type, const IRECT& bounds)
{
  // Letterbox the source aspect ratio inside `bounds` — no distortion.
  const float src = aspectFor(type);
  const float dst = bounds.W() / bounds.H();
  IRECT r = bounds;
  if (dst > src) {
    // Container wider than source — letterbox horizontally.
    const float w = bounds.H() * src;
    r = IRECT(bounds.MW() - w * 0.5f, bounds.T,
              bounds.MW() + w * 0.5f, bounds.B);
  } else {
    // Container taller than source — letterbox vertically.
    const float h = bounds.W() / src;
    r = IRECT(bounds.L, bounds.MH() - h * 0.5f,
              bounds.R, bounds.MH() + h * 0.5f);
  }

  g.DrawSVG(svg, r);
}

T3kGearIcon::T3kGearIcon(const IRECT& bounds, GearType type)
: IControl(bounds)
, mType(type)
{
  // mSvg is std::nullopt by default — populated on first Draw when
  // IGraphics is guaranteed alive (ISVG has no default constructor).
}

void T3kGearIcon::setType(GearType t)
{
  if (t == mType) return;
  mType = t;
  // Invalidate the cached SVG so the next Draw reloads the new file.
  mSvg.reset();
  SetDirty(false);
}

void T3kGearIcon::Draw(IGraphics& g)
{
  if (!mSvg.has_value())
    mSvg.emplace(g.LoadSVG(filenameFor(mType)));

  drawInto(g, *mSvg, mType, mRECT);
}

}  // namespace t3k::ui
