// T3kReadout — large live numeric readout (yellow mono numerals + dim
// uppercase param-name label below). The parent calls setActive(name,
// value) when any focused-panel knob is touched. The readout keeps
// the most-recent value after OnMouseUp.
//
// Display only — no input. SetIgnoreMouse(true).

#pragma once

#include <string>
#include "IControl.h"

namespace t3k::ui {

class T3kReadout : public iplug::igraphics::IControl
{
public:
  explicit T3kReadout(const iplug::igraphics::IRECT& bounds);

  void setActive(std::string paramName, std::string formattedValue);

  void Draw(iplug::igraphics::IGraphics& g) override;

private:
  std::string mParamName;
  std::string mValue;
};

}  // namespace t3k::ui
