// DownloadsView.h — Phase 2 placeholder for the downloads-status overlay.
//
// Overlay popup (NOT a full tab) — ToneRoot positions it as a floating
// surface. Therefore this view paints its own rounded background + 1px
// border so it reads as a distinct popover against the tab content
// underneath. Real download queue + progress arrives in Phase 7.

#pragma once

#include "IControl.h"

namespace t3k::ui {

class DownloadsView : public iplug::igraphics::IControl {
public:
  explicit DownloadsView(const iplug::igraphics::IRECT& bounds);
  void Draw(iplug::igraphics::IGraphics& g) override;
};

}  // namespace t3k::ui
