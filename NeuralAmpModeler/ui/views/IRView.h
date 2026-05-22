// IRView.h — Phase 2 placeholder for the Cabinet / IR tab.
//
// Full-tab view that ToneRoot sizes to the body area. Renders a centered
// "coming in Phase 7" hint. Real IR loading + cab simulation arrives later.

#pragma once

#include "IControl.h"

namespace t3k::ui {

class IRView : public iplug::igraphics::IControl {
public:
  explicit IRView(const iplug::igraphics::IRECT& bounds);
  void Draw(iplug::igraphics::IGraphics& g) override;
};

}  // namespace t3k::ui
