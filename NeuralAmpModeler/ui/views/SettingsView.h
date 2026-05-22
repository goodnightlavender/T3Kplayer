// SettingsView.h — Phase 2 placeholder for the settings overlay.
//
// Overlay popup (NOT a full tab) — same surface treatment as DownloadsView:
// own rounded background + 1px border so it sits cleanly on top of the
// active tab. Real preferences UI arrives in Phase 5.

#pragma once

#include "IControl.h"

namespace t3k::ui {

class SettingsView : public iplug::igraphics::IControl {
public:
  explicit SettingsView(const iplug::igraphics::IRECT& bounds);
  void Draw(iplug::igraphics::IGraphics& g) override;
};

}  // namespace t3k::ui
