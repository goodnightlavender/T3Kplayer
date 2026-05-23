// T3kRenameOverlay — compact inline text-entry overlay (Phase 3).
//
// Anchored under a library row by the parent (LibraryView). On show,
// opens iPlug2's CreateTextEntry overlay pre-filled with the current
// display name. Commit on Enter (or focus loss); cancel on Esc.
//
// Visual: kBgSurface card, 1px kBorder, kRadiusMd. Small "Rename to"
// label above the entry rect.

#pragma once

#include <functional>
#include <string>

#include "IControl.h"

namespace t3k::ui {

class T3kRenameOverlay : public iplug::igraphics::IControl {
 public:
  using OnSave = std::function<void(const std::string& newName)>;

  T3kRenameOverlay(const iplug::igraphics::IRECT& bounds,
                   OnSave onSave = {});

  // Re-seed the value and reposition under `anchorBounds`. The overlay
  // appears centered horizontally on the anchor's left edge and below
  // its bottom edge, clamped to the parent rect. Also opens the
  // platform text entry overlay automatically.
  void show(const iplug::igraphics::IRECT& anchorBounds,
            const std::string& initial);

  void setOnSave(OnSave cb) { mOnSave = std::move(cb); }

  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) override;
  void OnTextEntryCompletion(const char* str, int valIdx) override;

 private:
  // Open the platform text entry over the rendered entry rect.
  void openTextEntry();

  std::string mValue;
  OnSave      mOnSave;
};

}  // namespace t3k::ui
