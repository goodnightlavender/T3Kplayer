// T3kBadge — small, non-interactive label badge.
//
// Rounded rect (radius kRadiusSm), 1px kBorder outline, transparent fill,
// muted-text label centered. Used for tag-like status indicators.

#pragma once

#include "IControl.h"
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;

class T3kBadge : public IControl
{
public:
  T3kBadge(const IRECT& bounds, const char* label);

  void Draw(IGraphics& g) override;

  // Non-interactive — clicks pass through.
  bool IsHit(float /*x*/, float /*y*/) const override { return false; }

private:
  const char* mLabel;
};

}  // namespace t3k::ui
