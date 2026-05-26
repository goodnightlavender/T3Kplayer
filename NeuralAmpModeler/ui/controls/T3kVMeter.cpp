#include "T3kVMeter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "IGraphics.h"
#include "ISender.h"          // ISenderData<MAXNC, std::pair<float,float>>
#include "IPlugUtilities.h"   // AmpToDB

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
  // Delta gate — meters get updated at audio-rate via OnMsgFromDelegate;
  // many of those updates don't move the rendered bar at all. Skip the
  // repaint when nothing visible would change.
  constexpr double kEps = 1e-4;
  if (std::abs(level0to1 - mLevel)  < kEps &&
      std::abs(peak0to1  - mPeak)   < kEps &&
      std::abs(peakDb    - mPeakDb) < 0.05)
    return;
  mLevel  = level0to1;
  mPeak   = peak0to1;
  mPeakDb = peakDb;
  SetDirty(false);
}

// 2026-05-26 (Phase G2) — IPeakAvgSender<1> payload decoder. The plugin
// emits one packet per ProcessBlock through mInputSender / mOutputSender,
// each tagged with kCtrlTagInputMeter / kCtrlTagOutputMeter respectively.
// iPlug2 dispatches the packet to whichever IControl was attached with
// that tag — by AttachControl(meter, kCtrlTagXxxMeter) in T3kFocusedSlot.
//
// Channel layout matches the upstream IVPeakAvgMeterControl: vals[c] is
// std::pair<float,float>(peak, avg). Plugin is mono internally, so we
// take channel 0 (or chanOffset if non-zero) and ignore the rest.
//
// dB→bar-fraction mapping matches the visual range we want shown in the
// focused-slot panel: -60 dBFS at the bottom, +12 dBFS at the top.
void T3kVMeter::OnMsgFromDelegate(int msgTag, int dataSize, const void* pData)
{
  using PairT      = std::pair<float, float>;
  using SenderData = iplug::ISenderData<1, PairT>;

  if (msgTag != iplug::ISender<1, 64, PairT>::kUpdateMessage) return;
  if (!pData) return;
  if (dataSize < static_cast<int>(sizeof(SenderData))) return;

  const auto* d = static_cast<const SenderData*>(pData);
  const int c   = std::clamp(d->chanOffset, 0, 0);  // mono
  const float peakAmp = std::get<0>(d->vals[c]);
  const float avgAmp  = std::get<1>(d->vals[c]);

  const double peakDb = (peakAmp > 0.f) ? iplug::AmpToDB(static_cast<double>(peakAmp)) : -80.0;
  const double avgDb  = (avgAmp  > 0.f) ? iplug::AmpToDB(static_cast<double>(avgAmp))  : -80.0;

  // Map dBFS into the bar's 0..1 fraction over a -60..+12 dB window. Matches
  // the LOW/HIGH range that IVPeakAvgMeterControl uses by default.
  constexpr double kLowDb  = -60.0;
  constexpr double kHighDb = 12.0;
  constexpr double kRange  = kHighDb - kLowDb;
  auto toFrac = [&](double db) -> double {
    return std::clamp((db - kLowDb) / kRange, 0.0, 1.0);
  };

  setLevel(toFrac(avgDb), toFrac(peakDb), peakDb);
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
