// T3kGateHeaderPill — compact pill with GATE label + tiny ring +
// numeric dB readout. Drag-vertical adjusts kNoiseGateThreshold via
// IKnobControlBase.

#pragma once

#include "IControl.h"

namespace t3k::ui {

class T3kGateHeaderPill : public iplug::igraphics::IKnobControlBase
{
public:
  T3kGateHeaderPill(const iplug::igraphics::IRECT& bounds, int paramIdx);
  void Draw(iplug::igraphics::IGraphics& g) override;
};

}  // namespace t3k::ui
