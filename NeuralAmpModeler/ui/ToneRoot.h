// ToneRoot.h
// Root view for TONE3000 Player. Owns:
//   - A single-row header (logo + loose undo/redo glyphs · centered tabs ·
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

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

#include "IControl.h"
#include "IGraphics.h"

// Forward-declare upstream plug-in to avoid pulling its full header chain in.
class NeuralAmpModeler;

namespace t3k::cloud { struct SessionEvent; }

namespace t3k::ui {

// Forward declarations for child control / view types — full headers are
// only needed in the implementation file.
class T3kLogo;
class T3kTabBar;
class T3kLooseGlyph;
class T3kPresetPill;
class T3kPresetOverlay;
class T3kClickBackdrop;
class T3kFirstRunModal;
class T3kRestoreModal;
class T3kSettingsModal;
class T3kSignInPill;
class T3kAccountMenu;
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
  ~ToneRoot() override;

  // IControl overrides.
  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnResize() override;
  void OnAttached() override;  // Instantiate children once IGraphics is alive.
  // Routes the Save-As… name from the preset overlay (we open the
  // text entry on the overlay's behalf because iPlug2's
  // OnTextEntryCompletion fires on the control that initiated the
  // entry — here that's us).
  void OnTextEntryCompletion(const char* str, int valIdx) override;
  // Avatar circle is drawn directly by ToneRoot (not a child IControl)
  // so we intercept clicks here. Routes avatar-rect clicks to the
  // account-menu toggle.
  void OnMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) override;

  // Tab + overlay control.
  void switchTab(Tab tab);
  void togglePresetOverlay();
  void toggleAccountMenu();

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
  T3kLogo*         mLogo        = nullptr;
  T3kLooseGlyph*   mUndoGlyph   = nullptr;
  T3kLooseGlyph*   mRedoGlyph   = nullptr;
  T3kTabBar*       mTabBar      = nullptr;
  T3kPresetPill*   mPresetPill  = nullptr;
  // Phase 5 auth surface — pill OR avatar shows depending on Session
  // state; the account menu drops under the avatar/pill when clicked.
  T3kSignInPill*   mSignInPill  = nullptr;
  T3kAccountMenu*  mAccountMenu = nullptr;
  T3kClickBackdrop* mAccountBackdrop = nullptr;

  // Tab body views — all created once, visibility toggled by switchTab.
  ToneView*    mToneView    = nullptr;
  CloudView*   mCloudView   = nullptr;
  LibraryView* mLibraryView = nullptr;

  // Overlays — created once, hidden by default. The preset overlay is
  // toggled via the pill; the legacy Downloads / Settings overlays stay
  // attached for later phases but are no longer reachable from the v6
  // header.
  T3kPresetOverlay* mPresetOverlay = nullptr;
  // Invisible full-window click-catcher attached just below mPresetOverlay
  // in z-order while the overlay is visible. Click → closes the overlay.
  T3kClickBackdrop* mPresetBackdrop = nullptr;
  DownloadsView*    mDownloadsView = nullptr;
  SettingsView*     mSettingsView  = nullptr;
  // First-run modal — attached only when settings.tone3000_root is
  // empty; hidden + ignored otherwise. Attached BEFORE everything else
  // so it lives at the top of the z-order and intercepts clicks
  // (Decision: keep the dim layer above the tab body until the user
  // picks a folder).
  T3kFirstRunModal* mFirstRunModal = nullptr;
  // Phase 8 restore modal — attached above everything, hidden by
  // default. ToneRoot subscribes to LibrarySync::setPullListener and
  // flips the modal on when the pull reports a non-zero entry count
  // AND LibraryDb has at least one row with missing=1.
  // mRestoreModalShownOnce: 2026-05-25 fix — the pull listener fires
  // every time the session restores (which is every plug-in load with
  // a valid refresh token), and a real cloud library typically has
  // tones the user hasn't pulled to disk yet. Without this guard the
  // modal nagged on every launch. Reset path: the user signs out and
  // back in, OR they reload the plug-in instance.
  T3kRestoreModal* mRestoreModal = nullptr;
  bool             mRestoreModalShownOnce = false;
  // Phase 10 settings modal — attached hidden; account-menu's
  // onSettings callback flips it on.
  T3kSettingsModal* mSettingsModal = nullptr;
  // LibrarySync pull-listener id is not exposed (the LibrarySync API
  // is "last writer wins" — there's no id-based unsubscribe path). On
  // teardown we just stop LibrarySync, which is itself idempotent.

  Tab mActiveTab = Tab::Tone;

  // Phase 5 auth-surface state.
  // - mSessionListenerId: subscription token returned by cloud::Session;
  //   unsubscribed in the dtor.
  // - mSignedIn: cached Session state so Draw() can swap pill/avatar
  //   without taking the Session mutex on every paint.
  // - mSignInStatus: transient toast-style line drawn under the
  //   header (e.g. "Sign in: OAuth client_id not configured"). Auto-
  //   clears after `mSignInStatusExpiry` passes. Written from the
  //   OAuth worker thread; read on the GUI thread under the mutex.
  int                                  mSessionListenerId = 0;
  bool                                 mSignedIn          = false;
  mutable std::mutex                   mSignInStatusMtx;
  std::string                          mSignInStatus;
  std::chrono::steady_clock::time_point mSignInStatusExpiry;

  // Hide all three body views (used during tab switching).
  void hideAllBodies();

  // Build + attach the preset overlay (called from OnAttached, and again
  // from recreatePresetOverlayOnTop). Sets mPresetOverlay.
  void attachPresetOverlay(bool startVisible, int64_t activeId);

  // ── Phase 3: PresetStore-backed save / load ─────────────────────
  // Re-query PresetStore and push the result into mPresetOverlay.
  void refreshPresetList();
  // Snapshot ToneView's chain + knobs + write through to PresetStore.
  void saveCurrentPreset();
  // Save under a new name (or update if it already exists).
  void saveAsPreset(const std::string& name);
  // Load PresetStore[id] into ToneView via applyPresetState.
  void loadPreset(int64_t presetId);
  // Helper: update the pill label to reflect the active preset.
  void syncPillToActivePreset();

  // Destroy the existing overlay and re-attach a fresh one so it lands at
  // the end of IGraphics's control list (top of z-order). Preserves the
  // active-preset selection. Required after ToneView::rebuildStrip()
  // pushes new strip tiles onto the control list — iPlug2 doesn't expose
  // a "move to front" or "detach without delete" path at this revision.
  void recreatePresetOverlayOnTop();

  // ── Phase 5 helpers ───────────────────────────────────────────
  // Build the account menu + backdrop. Called from OnAttached, and
  // again from recreateAccountMenuOnTop to keep the overlay at the
  // top of the z-order (same pattern as the preset overlay).
  void attachAccountMenu(bool startVisible);
  void recreateAccountMenuOnTop();
  // Listen for cloud::Session state transitions and flip pill/avatar
  // visibility + repopulate the account-menu items.
  void onSessionEvent(const ::t3k::cloud::SessionEvent& ev);
  // Refresh the menu's items based on the current Session state.
  // Mock sign-in is gated behind `#ifdef _DEBUG` inside this helper.
  void refreshAccountMenuItems();
  // Compute the on-screen rect under the avatar where the menu lands.
  iplug::igraphics::IRECT accountMenuRect() const;
  // Toast surfacing — store + auto-expire a 4-second status line
  // under the header. Thread-safe.
  void setSignInStatus(const std::string& msg, int durationMs = 4000);
};

}  // namespace t3k::ui
