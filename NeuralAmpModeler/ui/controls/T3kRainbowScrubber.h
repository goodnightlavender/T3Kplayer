// T3kRainbowScrubber — horizontal seek bar with a red→yellow→blue gradient.
//
// Visual:
//   - 3px-tall horizontal track centered vertically, filled with a linear
//     gradient (kRainbowR at 0%, kRainbowY at 50%, kRainbowB at 100%).
//   - 9px white thumb at the current value's horizontal position, with a
//     2px black halo behind it for legibility on bright track colors.
//
// Value range: 0.0–1.0. Dragging snaps the thumb proportionally to mouse X
// and fires the onSeek callback with the new value.

#pragma once

#include <functional>

#include "IControl.h"
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::ISliderControlBase;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;

class T3kRainbowScrubber : public ISliderControlBase
{
public:
  T3kRainbowScrubber(const IRECT& bounds, std::function<void(float)> onSeek);

  void Draw(IGraphics& g) override;

private:
  std::function<void(float)> mOnSeek;
};

}  // namespace t3k::ui
