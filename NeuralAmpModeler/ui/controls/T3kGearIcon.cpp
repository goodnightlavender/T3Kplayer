// T3kGearIcon implementation. See T3kGearIcon.h.

#include "T3kGearIcon.h"

#include "IGraphics.h"
#include "../../config.h"  // ICON_{PEDAL,AMP,CAB,OUTBOARD,FULLRIG}_FN

namespace t3k::ui {

using namespace iplug::igraphics;

namespace {

// Source SVG viewBox aspect ratios (width/height). Used to letterbox the
// icon inside mRECT so the silhouette never distorts when the parent slot
// resizes. Kept here (not in T3kSlot) so the slot doesn't need to know
// per-type metrics.
constexpr float kAspect_Pedal    = 40.f / 46.f;  // tall pedal
constexpr float kAspect_Amp      = 50.f / 24.f;  // wide amp head
constexpr float kAspect_Cab      = 56.f / 36.f;  // wide cab
constexpr float kAspect_Outboard = 56.f / 32.f;  // wide rack unit
constexpr float kAspect_FullRig  = 56.f / 52.f;  // squarish

float sourceAspect(GearType t)
{
  switch (t) {
    case GearType::Pedal:    return kAspect_Pedal;
    case GearType::Amp:      return kAspect_Amp;
    case GearType::Cab:      return kAspect_Cab;
    case GearType::Outboard: return kAspect_Outboard;
    case GearType::FullRig:  return kAspect_FullRig;
  }
  return 1.f;
}

}  // namespace

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

const char* T3kGearIcon::svgFilename() const
{
  switch (mType) {
    case GearType::Pedal:    return ICON_PEDAL_FN;
    case GearType::Amp:      return ICON_AMP_FN;
    case GearType::Cab:      return ICON_CAB_FN;
    case GearType::Outboard: return ICON_OUTBOARD_FN;
    case GearType::FullRig:  return ICON_FULLRIG_FN;
  }
  return ICON_PEDAL_FN;  // unreachable; fallback to pedal
}

void T3kGearIcon::Draw(IGraphics& g)
{
  if (!mSvg.has_value())
    mSvg.emplace(g.LoadSVG(svgFilename()));

  // Letterbox the source aspect ratio inside mRECT — no distortion.
  const float src = sourceAspect(mType);
  const float dst = mRECT.W() / mRECT.H();
  IRECT r = mRECT;
  if (dst > src) {
    // Container wider than source — letterbox horizontally.
    const float w = mRECT.H() * src;
    r = IRECT(mRECT.MW() - w * 0.5f, mRECT.T,
              mRECT.MW() + w * 0.5f, mRECT.B);
  } else {
    // Container taller than source — letterbox vertically.
    const float h = mRECT.W() / src;
    r = IRECT(mRECT.L, mRECT.MH() - h * 0.5f,
              mRECT.R, mRECT.MH() + h * 0.5f);
  }

  g.DrawSVG(*mSvg, r);
}

}  // namespace t3k::ui
