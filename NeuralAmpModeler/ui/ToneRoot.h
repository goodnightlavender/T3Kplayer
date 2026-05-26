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
#include <vector>

#include "IControl.h"
#include "IGraphics.h"

#include "../library/PresetState.h"  // for UndoEntry

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
class T3kDownloadsPill;
class T3kDownloadsPopover;
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
  void toggleDownloadsPopover();
  // Pop the Settings modal. Used both by the account-menu "Settings..."
  // entry and the Ctrl+Shift+S global hotkey. Closes the account menu
  // first if it's open so its z-order doesn't fight the modal backdrop.
  void openSettings();

  // Routes popup-menu selections from per-preset Rename/Delete + the
  // confirm-delete menu back through PresetStore.
  void OnPopupMenuSelection(iplug::igraphics::IPopupMenu* pSelectedMenu,
                            int valIdx) override;

  // 2026-05-25 — periodic hook used to drive deferred actions (see
  // DeferredAction below). Called by iPlug2 when the editor is idle
  // (no dirty controls for ~10 frames). We use it as a "next tick"
  // queue to break out of the popup-menu callback chain when opening
  // a nested confirm popup — opening a new popup directly from
  // OnPopupMenuSelection corrupts iPlug2's mInPopupMenu state and
  // crashes the host (observed in Ableton on preset delete).
  void OnGUIIdle() override;

private:
  // Layout helpers — computed in OnResize, used in OnAttached to size children.
  iplug::igraphics::IRECT mHeaderRect;
  iplug::igraphics::IRECT mBodyRect;

  // Header sub-rects (single row).
  iplug::igraphics::IRECT mLogoRect;
  iplug::igraphics::IRECT mUndoRect;
  iplug::igraphics::IRECT mRedoRect;
  iplug::igraphics::IRECT mTabStripRect;
  iplug::igraphics::IRECT mDownloadsPillRect;
  iplug::igraphics::IRECT mPresetPillRect;
  iplug::igraphics::IRECT mAvatarRect;

  // Reference to the plug-in for parameter wiring (read-only).
  NeuralAmpModeler& mPlugin;

  // Header children (owned by IGraphics after AttachControl).
  T3kLogo*         mLogo        = nullptr;
  T3kLooseGlyph*   mUndoGlyph   = nullptr;
  T3kLooseGlyph*   mRedoGlyph   = nullptr;
  T3kTabBar*       mTabBar      = nullptr;
  T3kDownloadsPill*    mDownloadsPill     = nullptr;
  T3kDownloadsPopover* mDownloadsPopover  = nullptr;
  T3kClickBackdrop*    mDownloadsBackdrop = nullptr;
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
  // Rebuild the SettingsModal at the END of IGraphics' control list
  // so it z-orders above tab-body content (cloud cards, library
  // cards) which attach lazily after OnAttached. Same dance as
  // recreatePresetOverlayOnTop.
  void recreateSettingsModalOnTop();
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

  // ── Polish 3c: undo/redo for chain mutations ─────────────────────
  //
  // Tiny LIFO stack of (before, after) snapshots taken via
  // ToneView::snapshotPresetState. Scope is intentionally narrow —
  // we cover "load preset" and "load model from library". Per-knob
  // tweaks and per-slot drag-reorder live inside ToneView and would
  // need deeper hooks; they aren't included in the undo trace yet.
  //
  // mUndoApplyInProgress suppresses re-entrant pushUndo calls during
  // applyPresetState (ToneView fires its own mutation callbacks).
  struct UndoEntry {
    ::t3k::library::PresetState before;
    ::t3k::library::PresetState after;
  };
  std::vector<UndoEntry> mUndoStack;
  std::vector<UndoEntry> mRedoStack;
  bool                   mUndoApplyInProgress = false;

  // Snapshot current state and push the partial entry. The matching
  // commitUndo writes the "after" half once the mutation lands. If
  // the mutation never lands the partial entry stays — that's fine,
  // a stale "before" still rolls back correctly.
  void pushUndo();
  void commitUndo();
  void undo();
  void redo();
  // Flip glyph enabled-state to match stack contents.
  void updateUndoGlyphs();

  // Per-preset row right-click context — set by the overlay's row
  // right-click handler before iPlug2's popup-menu mechanism opens
  // the menu via CreatePopupMenu. Used by OnPopupMenuSelection to
  // know which preset to rename / delete.
  int64_t     mPendingPresetMenuId = 0;
  std::string mPendingPresetMenuName;
  // The "currently active" popup-menu kind, so OnPopupMenuSelection
  // can route the selection correctly (multiple menus share the
  // single OnPopupMenuSelection hook).
  enum class PendingMenuKind {
    None,
    PresetRowAction,    // Rename / Delete
    PresetDeleteConfirm,
  };
  PendingMenuKind mPendingMenuKind = PendingMenuKind::None;

  // Phase 3c: Inline Save-As text entry, opened by the preset
  // overlay's "SAVE AS…" button. The flag tells OnTextEntryCompletion
  // whether it's receiving a Save-As name (true) or a Rename name
  // (false).
  enum class PendingTextEntry { None, SaveAs, RenamePreset };
  PendingTextEntry mPendingTextEntry = PendingTextEntry::None;

  // 2026-05-25 — deferred-action queue. iPlug2's
  // SetControlValueAfterPopupMenu reads mInPopupMenu BOTH at function
  // entry AND again after dispatching to OnPopupMenuSelection; if the
  // selection handler opens a nested CreatePopupMenu, the recursive
  // SetControlValueAfterPopupMenu sets mInPopupMenu=nullptr on its
  // way out and the outer frame then dereferences null -> host crash
  // (observed in Ableton when picking "Delete" on a preset row).
  // We defer the nested popup to the next idle tick instead. Carries
  // its own captured preset name/id so the deferred handler can re-
  // open the confirm popup with the right label after the outer
  // callback chain has unwound.
  enum class DeferredAction : uint8_t {
    None,
    OpenDeleteConfirm,
  };
  DeferredAction mPendingDeferredAction = DeferredAction::None;
};

}  // namespace t3k::ui
