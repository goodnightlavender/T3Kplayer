// T3kVMeter — vertical level bar with peak-hold tick + dB numerical
// readout below. Layout inside its rect: small uppercase label up
// top, bar fills the middle (stretches to whatever height it gets),
// dB numeric at the bottom.

#pragma once

#include "IControl.h"

namespace t3k::ui {

class T3kVMeter : public iplug::igraphics::IControl
{
public:
  enum class Label { In, Out };

  T3kVMeter(const iplug::igraphics::IRECT& bounds, Label label);

  void setLevel(double level0to1, double peak0to1, double peakDb);

  void Draw(iplug::igraphics::IGraphics& g) override;

private:
  Label mLabel;
  double mLevel = 0.0;
  double mPeak  = 0.0;
  double mPeakDb = -80.0;
};

}  // namespace t3k::ui
