// T3kRainbowScrubber implementation. See T3kRainbowScrubber.h.

#include "T3kRainbowScrubber.h"

#include <algorithm>

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

constexpr float kTrackHeight = 3.f;
constexpr float kThumbRadius = 4.5f;  // 9px diameter
constexpr float kThumbHaloPad = 2.f;  // halo extends 2px beyond thumb

const IColor kBlack(255, 0, 0, 0);
const IColor kWhite(255, 255, 255, 255);

}  // namespace

T3kRainbowScrubber::T3kRainbowScrubber(const IRECT& bounds,
                                       std::function<void(float)> onSeek)
: ISliderControlBase(bounds,
                     /*actionFunc*/ nullptr,
                     EDirection::Horizontal,
                     /*gearing*/ 1.0,
                     /*handleSize*/ kThumbRadius * 2.f)
, mOnSeek(std::move(onSeek))
{
  // Forward value changes from ISliderControlBase's mouse handling out to
  // the caller. The action func fires on every SetDirty during drag.
  SetActionFunction([this](IControl* /*caller*/) {
    if (mOnSeek) mOnSeek(static_cast<float>(GetValue()));
  });
}

void T3kRainbowScrubber::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // ── Track ─────────────────────────────────────────────────────────────
  const float cy = mRECT.MH();
  const IRECT track(mRECT.L, cy - kTrackHeight * 0.5f,
                    mRECT.R, cy + kTrackHeight * 0.5f);

  const IPattern rainbow = IPattern::CreateLinearGradient(
      track.L, cy, track.R, cy,
      {
        IColorStop(th::kRainbowR, 0.0f),
        IColorStop(th::kRainbowY, 0.5f),
        IColorStop(th::kRainbowB, 1.0f),
      });

  g.PathClear();
  g.PathRect(track);
  g.PathFill(rainbow);

  // ── Thumb ─────────────────────────────────────────────────────────────
  const float v = float(std::clamp(GetValue(), 0.0, 1.0));
  const float tx = mRECT.L + v * mRECT.W();

  // Black halo first (slightly larger), then the white thumb on top.
  g.FillCircle(kBlack, tx, cy, kThumbRadius + kThumbHaloPad);
  g.FillCircle(kWhite, tx, cy, kThumbRadius);
}

}  // namespace t3k::ui
