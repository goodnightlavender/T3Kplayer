#include "T3kVMeter.h"

#include <cstdio>
#include "IGraphics.h"
#include "../theme.h"

namespace t3k::ui {

using namespace iplug::igraphics;

T3kVMeter::T3kVMeter(const IRECT& bounds, Label label)
: IControl(bounds)
, mLabel(label)
{
  SetIgnoreMouse(true);
}

void T3kVMeter::setLevel(double level0to1, double peak0to1, double peakDb)
{
  mLevel  = level0to1;
  mPeak   = peak0to1;
  mPeakDb = peakDb;
  SetDirty(false);
}

void T3kVMeter::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  const float labelH = 12.f;
  const float numH   = 12.f;
  const IRECT labelR(mRECT.L, mRECT.T, mRECT.R, mRECT.T + labelH);
  const IRECT barR  (mRECT.L + (mRECT.W() - 10.f) * 0.5f,
                     mRECT.T + labelH + 4.f,
                     mRECT.L + (mRECT.W() + 10.f) * 0.5f,
                     mRECT.B - numH - 4.f);
  const IRECT numR  (mRECT.L, mRECT.B - numH, mRECT.R, mRECT.B);

  const IText lbl(8.f, th::kTextMuted, th::kFontBody, EAlign::Center, EVAlign::Middle);
  g.DrawText(lbl, mLabel == Label::In ? "IN" : "OUT", labelR);

  g.FillRoundRect(IColor(255, 26, 26, 26), barR, 2.f);

  const float fillTop = barR.B - barR.H() * static_cast<float>(mLevel);
  const IRECT fillR(barR.L, fillTop, barR.R, barR.B);
  g.FillRoundRect(th::kAccent, fillR, 2.f);

  if (mPeak > 0.0)
  {
    const float peakY = barR.B - barR.H() * static_cast<float>(mPeak);
    g.FillRect(th::kAccent,
               IRECT(barR.L - 2.f, peakY - 0.5f, barR.R + 2.f, peakY + 0.5f));
  }

  const IText numT(10.f, th::kText, th::kFontBody, EAlign::Center, EVAlign::Middle);
  char buf[16];
  if (mPeakDb <= -80.0)
    std::snprintf(buf, sizeof(buf), "-INF");
  else
    std::snprintf(buf, sizeof(buf), "%.1f", mPeakDb);
  g.DrawText(numT, buf, numR);
}

}  // namespace t3k::ui
