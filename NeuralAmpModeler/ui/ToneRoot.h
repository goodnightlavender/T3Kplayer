// ToneRoot.h
// Root view for TONE3000 Player. Owns:
//   - A single-row header (logo + loose ↶/↷ glyphs · centered tabs ·
//     preset pill + avatar) — Phase 2b v6 layout.
//   - The Tone-tab body (ToneView), which holds the slot strip, the
//     T3kModelInfoPane, and the 5 persistent tone knobs.
//   - LibraryView and CloudView bodies, sized to the same Body rect.
//   - The T3kPresetOverlay (Hide()-toggled by the preset pill).
//   - DownloadsView and SettingsView overlays — created and hidden;
//     Phase 2b doesn't trigger them from the header anymore (the v6
//     header has no Downloads pill or Settings cog).
//
// Construction: NeuralAmpModeler.cpp's mLayoutFunc attaches exactly ONE
// ToneRoot, which then attaches all its children via the IGraphics it's
// drawn into.

#pragma once

#include "IControl.h"
#include "IGraphics.h"

// Forward-declare upstream plug-in to avoid pulling its full header chain in.
class NeuralAmpModeler;

namespace t3k::ui {

// Forward declarations for child control / view types — full headers are
// only needed in the implementation file.
class T3kLogo;
class T3kTabBar;
class T3kLooseGlyph;
class T3kPresetPill;
class T3kPresetOverlay;
class ToneView;
class CloudView;
class LibraryView;
class DownloadsView;
class SettingsView;

class ToneRoot : public iplug::igraphics::IControl {
public:
  // Tab order — Tone → Library → Cloud per Decision 34.
  enum class Tab { Tone = 0, Library, Cloud, kCount };

  ToneRoot(const iplug::igraphics::IRECT& bounds, NeuralAmpModeler& plugin);

  // IControl overrides.
  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnResize() override;
  void OnAttached() override;  // Instantiate children once IGraphics is alive.

  // Tab + overlay control.
  void switchTab(Tab tab);
  void togglePresetOverlay();

private:
  // Layout helpers — computed in OnResize, used in OnAttached to size children.
  iplug::igraphics::IRECT mHeaderRect;
  iplug::igraphics::IRECT mBodyRect;

  // Header sub-rects (single row).
  iplug::igraphics::IRECT mLogoRect;
  iplug::igraphics::IRECT mUndoRect;
  iplug::igraphics::IRECT mRedoRect;
  iplug::igraphics::IRECT mTabStripRect;
  iplug::igraphics::IRECT mPresetPillRect;
  iplug::igraphics::IRECT mAvatarRect;

  // Reference to the plug-in for parameter wiring (read-only).
  NeuralAmpModeler& mPlugin;

  // Header children (owned by IGraphics after AttachControl).
  T3kLogo*        mLogo       = nullptr;
  T3kLooseGlyph*  mUndoGlyph  = nullptr;
  T3kLooseGlyph*  mRedoGlyph  = nullptr;
  T3kTabBar*      mTabBar     = nullptr;
  T3kPresetPill*  mPresetPill = nullptr;

  // Tab body views — all created once, visibility toggled by switchTab.
  ToneView*    mToneView    = nullptr;
  CloudView*   mCloudView   = nullptr;
  LibraryView* mLibraryView = nullptr;

  // Overlays — created once, hidden by default. The preset overlay is
  // toggled via the pill; the legacy Downloads / Settings overlays stay
  // attached for later phases but are no longer reachable from the v6
  // header.
  T3kPresetOverlay* mPresetOverlay = nullptr;
  DownloadsView*    mDownloadsView = nullptr;
  SettingsView*     mSettingsView  = nullptr;

  Tab mActiveTab = Tab::Tone;

  // Hide all three body views (used during tab switching).
  void hideAllBodies();
};

}  // namespace t3k::ui
