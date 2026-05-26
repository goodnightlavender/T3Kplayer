// T3kSectionHeader — small yellow uppercase letter-spaced label with
// a hairline rule beneath. Used by the MODEL INFO and SETTINGS
// columns inside ToneView's focused panel.

#pragma once

#include <string>
#include "IControl.h"

namespace t3k::ui {

class T3kSectionHeader : public iplug::igraphics::IControl
{
public:
  T3kSectionHeader(const iplug::igraphics::IRECT& bounds, std::string label);
  void Draw(iplug::igraphics::IGraphics& g) override;

private:
  std::string mLabel;
};

}  // namespace t3k::ui
