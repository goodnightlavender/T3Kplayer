// LibraryView.h — Phase 2 placeholder for the local library tab.
//
// Full-tab view that ToneRoot sizes to the body area. Renders a centered
// "coming in Phase 3" hint. Local model/IR browsing lands in Phase 3.

#pragma once

#include "IControl.h"

namespace t3k::ui {

class LibraryView : public iplug::igraphics::IControl {
public:
  explicit LibraryView(const iplug::igraphics::IRECT& bounds);
  void Draw(iplug::igraphics::IGraphics& g) override;
};

}  // namespace t3k::ui
