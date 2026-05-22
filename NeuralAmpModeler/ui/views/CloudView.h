// CloudView.h — Phase 2 placeholder for the cloud-browser tab.
//
// Full-tab view that ToneRoot sizes to the body area. Renders a centered
// "coming in Phase 6" hint. The real cloud browser (search, results,
// download) lands in Phase 6.

#pragma once

#include "IControl.h"

namespace t3k::ui {

class CloudView : public iplug::igraphics::IControl {
public:
  explicit CloudView(const iplug::igraphics::IRECT& bounds);
  void Draw(iplug::igraphics::IGraphics& g) override;
};

}  // namespace t3k::ui
