// ToneRoot.h
// Root view for TONE3000 Player. Owns the header (logo + search + downloads
// pill + avatar), the tab strip, the active-tab body slot, the persistent
// knob row at the bottom, and two popover overlays (Downloads, Settings).
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
class T3kSearchBar;
class T3kButton;
class T3kTabBar;
class T3kKnob;
class AmpView;
class IRView;
class CloudView;
class LibraryView;
class DownloadsView;
class SettingsView;

class ToneRoot : public iplug::igraphics::IControl {
public:
  enum class Tab { Amp = 0, IR, Cloud, Library, COUNT };

  ToneRoot(const iplug::igraphics::IRECT& bounds, NeuralAmpModeler& plugin);

  // IControl overrides.
  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnResize() override;
  void OnAttached() override;  // Instantiate children once IGraphics is alive.

  // Tab + overlay control.
  void switchTab(Tab tab);
  void toggleDownloadsOverlay();
  void toggleSettingsOverlay();

private:
  // Layout helpers — computed in OnResize, used in OnAttached to size children.
  iplug::igraphics::IRECT mHeaderRect;
  iplug::igraphics::IRECT mTabStripRect;
  iplug::igraphics::IRECT mBodyRect;
  iplug::igraphics::IRECT mKnobRowRect;

  // Header sub-rects.
  iplug::igraphics::IRECT mLogoRect;
  iplug::igraphics::IRECT mSearchRect;
  iplug::igraphics::IRECT mDownloadsRect;
  iplug::igraphics::IRECT mAvatarRect;

  // Reference to the plug-in for parameter wiring (read-only).
  NeuralAmpModeler& mPlugin;

  // Child IControl pointers (owned by IGraphics after AttachControl).
  T3kLogo*       mLogo          = nullptr;
  T3kSearchBar*  mSearchBar     = nullptr;
  T3kButton*     mDownloadsPill = nullptr;
  T3kTabBar*     mTabBar        = nullptr;
  T3kKnob*       mKnobIn        = nullptr;
  T3kKnob*       mKnobBass      = nullptr;
  T3kKnob*       mKnobMid       = nullptr;
  T3kKnob*       mKnobTreble    = nullptr;
  T3kKnob*       mKnobOut       = nullptr;

  // Tab body views — all created once, visibility toggled by switchTab.
  AmpView*       mAmpView     = nullptr;
  IRView*        mIRView      = nullptr;
  CloudView*     mCloudView   = nullptr;
  LibraryView*   mLibraryView = nullptr;

  // Overlays — created once, visibility toggled by header pill / avatar click.
  DownloadsView* mDownloadsView = nullptr;
  SettingsView*  mSettingsView  = nullptr;

  Tab mActiveTab = Tab::Amp;
  bool mDownloadsOpen = false;
  bool mSettingsOpen = false;

  // Hide all four body views (used during tab switching).
  void hideAllBodies();
};

}  // namespace t3k::ui
