// T3kGlobalKnob — compact 40-px rotary for chrome-level global
// controls (MASTER output). Inherits IKnobControlBase for drag +
// wheel + iPlug param binding.

#pragma once

#include <string>
#include "IControl.h"

namespace t3k::ui {

class T3kGlobalKnob : public iplug::igraphics::IKnobControlBase
{
public:
  T3kGlobalKnob(const iplug::igraphics::IRECT& bounds,
                int paramIdx,
                std::string label);

  void Draw(iplug::igraphics::IGraphics& g) override;

private:
  std::string mLabel;
};

}  // namespace t3k::ui
