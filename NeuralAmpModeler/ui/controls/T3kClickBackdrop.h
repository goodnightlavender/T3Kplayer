// T3kClickBackdrop — invisible full-window click target used to dismiss
// transient overlays (the preset dropdown, for one) when the user clicks
// anywhere outside their footprint.
//
// Usage: attach the backdrop just BEFORE the overlay in IGraphics' control
// list so the overlay z-orders above it. When the overlay opens, both
// become visible; when it closes, both hide. The backdrop's OnMouseDown
// fires `onClick` (which the overlay's owner wires to a "close overlay"
// callback) — that first click dismisses the overlay; the user clicks
// again to interact with whatever's underneath.
//
// The backdrop does not draw anything — Draw is a no-op. It exists solely
// for hit-test coverage of the area around the overlay.

#pragma once

#include <functional>

#include "IControl.h"

namespace t3k::ui {

class T3kClickBackdrop : public iplug::igraphics::IControl
{
public:
  T3kClickBackdrop(const iplug::igraphics::IRECT& bounds,
                   std::function<void()> onClick);

  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) override;

private:
  std::function<void()> mOnClick;
};

}  // namespace t3k::ui
