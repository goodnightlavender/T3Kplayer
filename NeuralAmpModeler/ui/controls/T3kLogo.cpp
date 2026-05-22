// T3kLogo.cpp
#include "T3kLogo.h"

#include "IGraphics.h"
#include "../../config.h"  // TONE3000_LOGO_FN

namespace t3k::ui {

using namespace iplug::igraphics;

T3kLogo::T3kLogo(const IRECT& bounds)
  : IControl(bounds)
  , mSvg{}  // loaded lazily on first Draw — IGraphics must be alive
{}

void T3kLogo::Draw(IGraphics& g)
{
  if (!mSvg.IsValid())
    mSvg = g.LoadSVG(TONE3000_LOGO_FN);

  // Scale to fit while preserving the source 210x32 aspect ratio.
  const float srcAspect = 210.f / 32.f;
  const float dstAspect = mRECT.W() / mRECT.H();
  IRECT dst = mRECT;
  if (dstAspect > srcAspect) {
    // Container wider than logo aspect — letterbox horizontally.
    const float w = mRECT.H() * srcAspect;
    dst = IRECT(mRECT.MW() - w * 0.5f, mRECT.T,
                mRECT.MW() + w * 0.5f, mRECT.B);
  } else {
    // Container taller than logo aspect — letterbox vertically.
    const float h = mRECT.W() / srcAspect;
    dst = IRECT(mRECT.L, mRECT.MH() - h * 0.5f,
                mRECT.R, mRECT.MH() + h * 0.5f);
  }

  g.DrawSVG(mSvg, dst);
}

}  // namespace t3k::ui
