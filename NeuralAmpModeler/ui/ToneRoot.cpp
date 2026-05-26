// ToneRoot.cpp — see ToneRoot.h for an overview of responsibilities.

#include "ToneRoot.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include "theme.h"
#include "layout.h"
#include "../config.h"  // ICON_UNDO_FN, ICON_REDO_FN

// Child controls + views.
#include "controls/T3kAccountMenu.h"
#include "controls/T3kClickBackdrop.h"
#include "controls/T3kDownloadsPill.h"
#include "controls/T3kDownloadsPopover.h"
#include "controls/T3kLogo.h"
#include "controls/T3kLooseGlyph.h"
#include "controls/T3kPresetOverlay.h"
#include "controls/T3kPresetPill.h"
#include "controls/T3kSignInPill.h"
#include "controls/T3kTabBar.h"
#include "views/CloudView.h"
#include "views/DownloadsView.h"
#include "views/LibraryView.h"
#include "views/SettingsView.h"
#include "views/T3kFirstRunModal.h"
#include "views/T3kRestoreModal.h"
#include "views/T3kSettingsModal.h"
#include "views/ToneView.h"

#include "../cloud/LibrarySync.h"
#include "../cloud/OAuthFlow.h"
#include "../cloud/Session.h"
#include "../cloud/SessionEvent.h"
#include "../library/LibraryScanner.h"
#include "../library/PresetStore.h"
#include "../library/PresetState.h"
#include "../settings/Settings.h"

namespace t3k::ui {

using namespace iplug::igraphics;

namespace {

// Layout constants — single 44px header row (Phase 2b v6).
constexpr float kHeaderH        = 44.f;
// 2026-05-26 — logo width bumped from 110 → 160 to give the new SVG
// wordmark more visual presence in the top bar. T3kLogo letterboxes
// internally to preserve the 247×30 aspect, so heavier widths just
// produce a taller-rendered glyph (the header itself is still capped
// by mHeaderRect's height).
constexpr float kLogoW          = 160.f;
constexpr float kLooseW         = 18.f;
constexpr float kLooseGap       = 4.f;
constexpr float kPresetPillW    = 160.f;
constexpr float kPresetPillH    = 24.f;
constexpr float kAvatarW        = 28.f;
constexpr float kPresetGap      = 10.f;  // between preset pill and avatar
constexpr float kPresetOverlayW = 280.f;
constexpr float kPresetOverlayH = 210.f;
constexpr float kDownloadsPillW = 60.f;
constexpr float kDownloadsPopoverW = 320.f;
constexpr float kDownloadsPopoverH = 260.f;

// Phase 5 — account menu sized to comfortably hold the header + 3 rows.
constexpr float kAccountMenuW = 220.f;
constexpr float kAccountMenuH = 140.f;

}  // namespace

ToneRoot::ToneRoot(const IRECT& bounds, NeuralAmpModeler& plugin)
  : IControl(bounds), mPlugin(plugin)
{
  // Compute initial sub-rects so OnAttached can size children correctly.
  OnResize();
}

ToneRoot::~ToneRoot()
{
  // Drop our Session subscription so the listener callback can't fire
  // into a dangling `this` after teardown. Safe to call even if the
  // subscribe never ran (signature treats id<=0 as no-op).
  if (mSessionListenerId > 0) {
    ::t3k::cloud::Session::instance().unsubscribe(mSessionListenerId);
    mSessionListenerId = 0;
  }
}

void ToneRoot::OnResize()
{
  // ── Vertical split: Header / Body ─────────────────────────────────
  auto split = t3k::layout::rowFixedTop(mRECT, kHeaderH);
  mHeaderRect = split.first;
  mBodyRect   = split.second;

  // ── Header partition: three groups ────────────────────────────────
  //   Left  : logo + loose ↶ + loose ↷
  //   Center: tab bar (centered horizontally)
  //   Right : preset pill + avatar
  const IRECT headerPad =
      t3k::layout::pad(mHeaderRect, t3k::theme::kS2, t3k::theme::kS4,
                                    t3k::theme::kS2, t3k::theme::kS4);

  // Left cluster.
  float lx = headerPad.L;
  mLogoRect = IRECT(lx, headerPad.T, lx + kLogoW, headerPad.B);
  lx = mLogoRect.R + t3k::theme::kS3;
  mUndoRect = IRECT(lx, headerPad.T, lx + kLooseW, headerPad.B);
  lx = mUndoRect.R + kLooseGap;
  mRedoRect = IRECT(lx, headerPad.T, lx + kLooseW, headerPad.B);

  // Right cluster (laid out from the right edge inward).
  float rx = headerPad.R;
  const float avatarSize = std::min(kAvatarW, headerPad.H());
  mAvatarRect = IRECT(rx - avatarSize, headerPad.MH() - avatarSize * 0.5f,
                      rx,              headerPad.MH() + avatarSize * 0.5f);
  rx = mAvatarRect.L - kPresetGap;
  mPresetPillRect = IRECT(rx - kPresetPillW,
                          headerPad.MH() - kPresetPillH * 0.5f,
                          rx,
                          headerPad.MH() + kPresetPillH * 0.5f);
  // Downloads pill — sits immediately to the LEFT of the preset pill,
  // separated by kS2.
  rx = mPresetPillRect.L - t3k::theme::kS2;
  mDownloadsPillRect = IRECT(rx - kDownloadsPillW,
                             headerPad.MH() - kPresetPillH * 0.5f,
                             rx,
                             headerPad.MH() + kPresetPillH * 0.5f);

  // Center cluster: tab bar fills the space between the loose-glyphs and
  // the downloads pill, but the T3kTabBar centers its own labels inside.
  const float tabL = mRedoRect.R + t3k::theme::kS3;
  const float tabR = mDownloadsPillRect.L - t3k::theme::kS3;
  mTabStripRect = IRECT(tabL, headerPad.T, tabR, headerPad.B);

  // Resize header children if they already exist.
  if (mLogo)       mLogo->SetTargetAndDrawRECTs(mLogoRect);
  if (mUndoGlyph)  mUndoGlyph->SetTargetAndDrawRECTs(mUndoRect);
  if (mRedoGlyph)  mRedoGlyph->SetTargetAndDrawRECTs(mRedoRect);
  if (mTabBar)     mTabBar->SetTargetAndDrawRECTs(mTabStripRect);
  if (mDownloadsPill) mDownloadsPill->SetTargetAndDrawRECTs(mDownloadsPillRect);
  if (mPresetPill) mPresetPill->SetTargetAndDrawRECTs(mPresetPillRect);
  // Sign-in pill occupies the avatar slot when signed-out. It's wider
  // than the avatar circle, so center-on-avatar would push the pill
  // past the right window edge. Anchor the pill's RIGHT edge to the
  // avatar's right edge so it stays inside the header padding.
  if (mSignInPill) {
    const float pillW = 84.f;
    const float pillH = 24.f;
    const float pillR = mAvatarRect.R;
    const float cy    = mAvatarRect.MH();
    mSignInPill->SetTargetAndDrawRECTs(
        IRECT(pillR - pillW, cy - pillH * 0.5f,
              pillR,         cy + pillH * 0.5f));
  }
  if (mAccountMenu) {
    mAccountMenu->SetTargetAndDrawRECTs(accountMenuRect());
  }
  if (mAccountBackdrop) mAccountBackdrop->SetTargetAndDrawRECTs(mRECT);

  // ── Body — all three tab views share mBodyRect ─────────────────────
  if (mToneView)    mToneView->SetTargetAndDrawRECTs(mBodyRect);
  if (mCloudView)   mCloudView->SetTargetAndDrawRECTs(mBodyRect);
  if (mLibraryView) mLibraryView->SetTargetAndDrawRECTs(mBodyRect);

  // ── Preset overlay — anchored under the preset pill's right edge ──
  if (mPresetOverlay) {
    const float top  = mPresetPillRect.B + t3k::theme::kS2;
    const float left = mPresetPillRect.R - kPresetOverlayW;
    mPresetOverlay->SetTargetAndDrawRECTs(
        IRECT(left, top, left + kPresetOverlayW, top + kPresetOverlayH));
  }
  // Backdrop is full-window so a click anywhere outside the overlay's
  // own footprint reaches it.
  if (mPresetBackdrop) mPresetBackdrop->SetTargetAndDrawRECTs(mRECT);

  // Downloads popover — anchored under the downloads pill's RIGHT edge,
  // expanding LEFT. The pill sits at the top-right corner of the UI and
  // the popover is wider than the pill, so anchoring to the left would
  // push the popover past the window edge and clip it. Right-align so
  // the popover stays inside the window.
  // 2026-05-25 — Polish round 4 fix.
  if (mDownloadsPopover) {
    const float top  = mDownloadsPillRect.B + t3k::theme::kS2;
    const float left = mDownloadsPillRect.R - kDownloadsPopoverW;
    mDownloadsPopover->SetTargetAndDrawRECTs(
        IRECT(left, top, left + kDownloadsPopoverW, top + kDownloadsPopoverH));
  }
  if (mDownloadsBackdrop) mDownloadsBackdrop->SetTargetAndDrawRECTs(mRECT);

  // Legacy overlays — hidden; bounded to avoid 0-sized rects.
  if (mDownloadsView) mDownloadsView->SetTargetAndDrawRECTs(IRECT(0.f, 0.f, 1.f, 1.f));
  if (mSettingsView)  mSettingsView->SetTargetAndDrawRECTs(IRECT(0.f, 0.f, 1.f, 1.f));

  // First-run modal covers the entire window.
  if (mFirstRunModal) mFirstRunModal->SetTargetAndDrawRECTs(mRECT);

  // Restore modal also covers the entire window (the card is centered
  // inside via its own OnResize).
  if (mRestoreModal) mRestoreModal->SetTargetAndDrawRECTs(mRECT);
  // Settings modal — same full-window backdrop.
  if (mSettingsModal) mSettingsModal->SetTargetAndDrawRECTs(mRECT);
}

void ToneRoot::OnAttached()
{
  IGraphics* g = GetUI();
  if (!g) return;

  // ── Header ───────────────────────────────────────────────────────
  mLogo = new T3kLogo(mLogoRect);
  g->AttachControl(mLogo);

  // Loose undo / redo glyphs. Undo enabled, Redo disabled by default
  // (matches the v6 mockup state). Phase 2b stubs the click handlers —
  // Phase 3 wires the real undo stack. The earlier revision passed UTF-8
  // for U+21B6 / U+21B7 here; Inter's vendored subset doesn't include
  // those code points so they rendered as tofu — see ICON_UNDO_FN /
  // ICON_REDO_FN SVG icons instead.
  mUndoGlyph = new T3kLooseGlyph(mUndoRect, ICON_UNDO_FN,
                                 /*onClick*/ [this]() { this->undo(); },
                                 /*disabled*/ true);
  mRedoGlyph = new T3kLooseGlyph(mRedoRect, ICON_REDO_FN,
                                 /*onClick*/ [this]() { this->redo(); },
                                 /*disabled*/ true);
  g->AttachControl(mUndoGlyph);
  g->AttachControl(mRedoGlyph);

  // Tab strip — Tone / Library / Cloud (Decision 34).
  mTabBar = new T3kTabBar(
      mTabStripRect,
      /*tabs*/ { "Tone", "Library", "Cloud" },
      /*onChanged*/ [this](int idx) { this->switchTab(static_cast<Tab>(idx)); },
      /*initial*/ static_cast<int>(Tab::Tone));
  g->AttachControl(mTabBar);

  // Downloads pill — toggles the popover below. Attached BEFORE the
  // preset pill in the control list so the preset pill stays last in
  // the right-cluster z-order (consistent with the existing pattern).
  mDownloadsPill = new T3kDownloadsPill(
      mDownloadsPillRect,
      /*onToggleOverlay*/ [this] { this->toggleDownloadsPopover(); });
  g->AttachControl(mDownloadsPill);

  // Preset pill — toggles the overlay below.
  mPresetPill = new T3kPresetPill(
      mPresetPillRect,
      /*onToggleOverlay*/ [this] { this->togglePresetOverlay(); });
  // Pull the active preset name from PresetStore (defaults to
  // "Default Setting" on a fresh install — ensureDefaults() guarantees
  // a row exists by the time we get here).
  if (auto st = ::t3k::library::PresetStore::instance().load(
          ::t3k::library::PresetStore::instance().activeId());
      st.has_value()) {
    // Pull the name from the list so we don't query PresetStore twice.
    for (const auto& r : ::t3k::library::PresetStore::instance().list()) {
      if (r.active) {
        mPresetPill->setActivePresetName(r.name);
        break;
      }
    }
  } else {
    mPresetPill->setActivePresetName("Default Setting");
  }
  mPresetPill->setDirty(false);
  g->AttachControl(mPresetPill);

  // ── Tab body views ──────────────────────────────────────────────
  mToneView    = new ToneView(mBodyRect, mPlugin);
  // Library click → load into ToneView's current slot (or first free
  // pedal slot if no selection — ToneView decides). Switch to the Tone
  // tab so the load is visible immediately.
  mLibraryView = new LibraryView(mBodyRect,
      [this](const std::string& toneId, const std::string& modelId) {
        if (!mToneView) return;
        // Snapshot the chain BEFORE the load so the user can undo it
        // (Polish 3c). commitUndo writes the "after" half once the
        // mutation lands.
        this->pushUndo();
        // -1 → ToneView::loadModelIntoSlot picks the first free pedal
        // slot. The user can still drop the model into a specific
        // slot by selecting that slot first (Phase 3.5 wiring).
        mToneView->loadModelIntoSlot(-1, toneId, modelId);
        switchTab(Tab::Tone);
        if (mPresetPill) mPresetPill->setDirty(true);
        this->commitUndo();
      });
  mCloudView   = new CloudView(mBodyRect);
  g->AttachControl(mToneView);
  g->AttachControl(mLibraryView);
  g->AttachControl(mCloudView);
  // Show only the initial tab.
  mLibraryView->Hide(true);
  mCloudView->Hide(true);

  // If the strip rebuilds while the overlay is open (chain add/remove
  // races a visible overlay — rare with current UX but defensive), the
  // strip's new tiles would be inserted at the end of mControls, in front
  // of the overlay. Recreate the overlay to push it back on top.
  mToneView->setOnStripRebuilt([this]() {
    if (mPresetOverlay && !mPresetOverlay->IsHidden()) {
      recreatePresetOverlayOnTop();
    }
  });
  // 2026-05-25 — the "+" tile on the chain strip now routes the user
  // to the Library tab (the real picker). LibraryView's LOAD INTO
  // CHAIN brings them back here.
  mToneView->setOnAddSlotRequested([this]() {
    this->switchTab(Tab::Library);
  });

  // ── Legacy overlays — hidden; no longer reachable from the v6 header.
  // Attach BEFORE the preset overlay so the latter stays at the very end
  // of the IGraphics control list (highest z-order). These two never show.
  mDownloadsView = new DownloadsView(IRECT(0.f, 0.f, 1.f, 1.f));
  mSettingsView  = new SettingsView(IRECT(0.f, 0.f, 1.f, 1.f));
  g->AttachControl(mDownloadsView);
  g->AttachControl(mSettingsView);
  mDownloadsView->Hide(true);
  mSettingsView->Hide(true);

  // ── Phase 5: sign-in pill — created hidden; toggled by session events.
  // Sized in OnResize() relative to mAvatarRect; gets a 1×1 placeholder
  // here so iPlug2 has a valid rect at attach time.
  mSignInPill = new T3kSignInPill(
      IRECT(0.f, 0.f, 1.f, 1.f),
      /*onClick*/ [this] {
        // Configured: real OAuth flow on a background thread. The
        // browser pops, the user authorizes, the loopback server
        // receives the callback, and SessionEvent::SignedIn lands on
        // mSessionListenerId — which flips mSignedIn and hides this
        // pill. Surface a "Signing in…" toast so the user has feedback
        // while the browser tab opens.
        if (::t3k::cloud::OAuthFlow::isConfigured()) {
          this->setSignInStatus("Signing in… check your browser", 8000);
          ::t3k::cloud::Session::instance().signIn();
          return;
        }
        // Unconfigured (fork operators who haven't set up their OAuth
        // app yet): surface a toast directing them at OAuthConfig.h
        // AND open the account menu so they can use mock-sign-in
        // immediately, without having to know to click the avatar.
        this->setSignInStatus("Configure OAuth client_id in OAuthConfig.h");
        this->toggleAccountMenu();
      });
  g->AttachControl(mSignInPill);
  // Initial state pulled from Session below.

  // ── Preset overlay — created hidden, must be attached last ────
  attachPresetOverlay(/*startVisible*/ false,
                      /*activeId*/ ::t3k::library::PresetStore::instance().activeId());

  // ── Downloads popover — backdrop + panel, attached AFTER preset
  //    overlay so the popover sits above strip tiles / cards. Same
  //    "recreate on top" dance applies when toggling visible.
  mDownloadsBackdrop = new T3kClickBackdrop(mRECT,
      /*onClick*/ [this] {
        if (mDownloadsPopover && !mDownloadsPopover->IsHidden())
          toggleDownloadsPopover();
      });
  g->AttachControl(mDownloadsBackdrop);
  mDownloadsBackdrop->Hide(true);

  mDownloadsPopover = new T3kDownloadsPopover(
      IRECT(0.f, 0.f, 1.f, 1.f),
      /*provider*/ [this]() {
        return mDownloadsPill ? mDownloadsPill->snapshotRows()
                              : std::vector<T3kDownloadsPill::Row>{};
      });
  g->AttachControl(mDownloadsPopover);
  mDownloadsPopover->Hide(true);

  // ── Phase 5: account menu — attached AFTER preset overlay so it
  //    sits at the top of the z-order (clicking the avatar after the
  //    preset overlay was just shown still works).
  attachAccountMenu(/*startVisible*/ false);

  // ── Phase 5: subscribe to Session for state transitions ───────
  mSignedIn =
      (::t3k::cloud::Session::instance().state() ==
       ::t3k::cloud::Session::State::SignedIn);
  if (mSignInPill) mSignInPill->Hide(mSignedIn);
  refreshAccountMenuItems();
  mSessionListenerId = ::t3k::cloud::Session::instance().subscribe(
      [this](const ::t3k::cloud::SessionEvent& ev) {
        this->onSessionEvent(ev);
      });

  // ── First-run modal ───────────────────────────────────────────
  // Attached LAST so it sits at the top of the z-order and intercepts
  // every mouse event until the user picks a folder. When the user
  // chooses, we persist the path, kick off an initial scan, and hide
  // the modal so the rest of the UI becomes interactive.
  const bool firstRun = ::t3k::settings::instance().tone3000_root.empty();
  if (firstRun) {
    mFirstRunModal = new T3kFirstRunModal(mRECT,
        [this](const std::string& chosenRoot) {
          ::t3k::settings::instance().tone3000_root = chosenRoot;
          ::t3k::settings::save();
          ::t3k::library::LibraryScanner::instance().rescan();
          if (mFirstRunModal) {
            mFirstRunModal->Hide(true);
            // Disable the modal's buttons too. The modal owns them via
            // OnAttached → they live in the IGraphics control list; we
            // simply hide them by hiding the parent modal.
          }
        });
    g->AttachControl(mFirstRunModal);
  } else {
    // Settings already point at a real folder — kick off a background
    // refresh so the library reflects any files added since last run.
    ::t3k::library::LibraryScanner::instance().rescan();
  }

  // ── Phase 8: restore-library modal ────────────────────────────
  // Attached AFTER the first-run modal so it sits at the top of the
  // z-order. Starts hidden; LibrarySync's pull listener flips it on
  // when a freshly-pulled library reveals tones that aren't on disk.
  mRestoreModal = new T3kRestoreModal(mRECT,
      /*onRestore*/ [this] {
        ::t3k::cloud::sync::LibrarySync::instance().restoreAllMissing(
            /*onDone*/ {});
        if (mRestoreModal) mRestoreModal->Hide(true);
      },
      /*onDismiss*/ [this] {
        if (mRestoreModal) mRestoreModal->Hide(true);
      });
  g->AttachControl(mRestoreModal);
  mRestoreModal->Hide(true);

  // ── Phase 10: settings modal. Attached after the restore modal so
  // both sit at the top of the z-order. Hidden by default; the account
  // menu's onSettings callback flips it on.
  mSettingsModal = new T3kSettingsModal(mRECT,
      /*onClose*/ [this] {
        if (mSettingsModal) mSettingsModal->Hide(true);
        SetDirty(false);
      });
  g->AttachControl(mSettingsModal);
  mSettingsModal->Hide(true);

  // Register the LibrarySync pull listener. Fires on the HTTP worker
  // thread; we only flip IControl::Hide + SetDirty here, which iPlug2
  // tolerates from any thread (same pattern as Session events that
  // hide/show the sign-in pill). Capturing `this` is safe because
  // ToneRoot outlives LibrarySync — the singleton is never destroyed
  // before the IGraphics teardown that drops this control.
  ::t3k::cloud::sync::LibrarySync::instance().setPullListener(
      [this](bool ok, int entries) {
        if (!ok || entries <= 0) return;
        // Only nag the user once per plug-in instance. The pull
        // listener fires every time the session restores (which
        // happens on every launch when a refresh token is on disk),
        // and a real cloud library typically has tones not yet
        // pulled — without this gate the modal pops on every load.
        if (mRestoreModalShownOnce) return;
        const int missing =
            ::t3k::cloud::sync::LibrarySync::instance().countLocalMissing();
        if (missing <= 0) return;
        if (!mRestoreModal) return;
        mRestoreModalShownOnce = true;
        mRestoreModal->setMissingCount(missing);
        mRestoreModal->Hide(false);
        SetDirty(false);
      });

  // Load the active preset's state into the chain so the UI restores
  // across DAW restarts. Falls back to the demo seed inside ToneView
  // if the preset is empty.
  const int64_t activeId = ::t3k::library::PresetStore::instance().activeId();
  if (activeId > 0 && mToneView) {
    auto st = ::t3k::library::PresetStore::instance().load(activeId);
    if (st.has_value() && !st->slots.empty()) {
      mToneView->applyPresetState(*st);
    }
  }

  // Resize all children to their proper bounds now that they exist.
  // (ToneView::OnResize now updates strip tile positions in place — it
  // does NOT detach/reattach, so the preset overlay we just attached
  // stays at the top of the z-order through this call.)
  OnResize();
}

void ToneRoot::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // The plug-in's panel background is solid black via AttachPanelBackground.
  // We draw the avatar circle + initials here since it has no dedicated
  // control. The avatar is hidden in the signed-out state — the
  // T3kSignInPill child takes its slot instead.
  if (mSignedIn) {
    const IRECT& a = mAvatarRect;
    const float cx = a.MW();
    const float cy = a.MH();
    const float r  = std::min(a.W(), a.H()) * 0.5f - 2.f;

    g.FillCircle(th::kBgSurface, cx, cy, r);
    g.DrawCircle(th::kBorder, cx, cy, r, nullptr, 1.f);

    // Initials — first char of username (uppercased) when known.
    // Defaults to "T" for the @testuser mock + the unknown-username
    // signed-in state.
    std::string initials = "T";
    if (auto u = ::t3k::cloud::Session::instance().currentUser();
        u.has_value() && !u->username.empty()) {
      initials = std::string(1, static_cast<char>(std::toupper(
          static_cast<unsigned char>(u->username[0]))));
    }
    g.DrawText(IText(th::kTypeSmall, th::kText,
                     th::kFontBodyBold, EAlign::Center, EVAlign::Middle),
               initials.c_str(),
               IRECT(cx - r, cy - r, cx + r, cy + r));
  }

  // 1px separator below the header row.
  g.FillRect(th::kBorder,
             IRECT(mRECT.L, mHeaderRect.B - 1.f, mRECT.R, mHeaderRect.B));

  // ── Phase 5 sign-in status line ────────────────────────────────
  // Drawn just below the header divider, centered. Auto-clears when
  // expiry passes.
  std::string status;
  {
    std::lock_guard<std::mutex> lk(mSignInStatusMtx);
    if (!mSignInStatus.empty()) {
      if (std::chrono::steady_clock::now() < mSignInStatusExpiry) {
        status = mSignInStatus;
      } else {
        mSignInStatus.clear();
      }
    }
  }
  if (!status.empty()) {
    const IRECT toastR(mRECT.L, mHeaderRect.B + 4.f,
                       mRECT.R, mHeaderRect.B + 22.f);
    g.DrawText(IText(th::kTypeSmall, th::kTextMuted,
                     th::kFontBody, EAlign::Center, EVAlign::Middle),
               status.c_str(), toastR);
  }
}

void ToneRoot::OnMouseDown(float x, float y, const IMouseMod& /*mod*/)
{
  // Avatar circle is drawn directly by ToneRoot — no child control
  // intercepts the click. Route avatar hits to the account menu when
  // we're signed-in. (Signed-out state uses the sign-in pill, which
  // IS a child IControl and handles its own clicks.)
  if (mSignedIn && mAvatarRect.Contains(x, y)) {
    toggleAccountMenu();
  }
}

void ToneRoot::switchTab(Tab tab)
{
  if (tab == mActiveTab) return;
  hideAllBodies();
  mActiveTab = tab;
  IControl* next = nullptr;
  switch (tab) {
    case Tab::Tone:    next = mToneView;    break;
    case Tab::Library: next = mLibraryView; break;
    case Tab::Cloud:   next = mCloudView;   break;
    default: break;
  }
  if (next) next->Hide(false);
}

void ToneRoot::hideAllBodies()
{
  if (mToneView)    mToneView->Hide(true);
  if (mLibraryView) mLibraryView->Hide(true);
  if (mCloudView)   mCloudView->Hide(true);
}

void ToneRoot::togglePresetOverlay()
{
  if (!mPresetOverlay) return;
  const bool willShow = mPresetOverlay->IsHidden();
  if (willShow) {
    // Ensure the overlay (and its backdrop) are the last controls in the
    // z-order before showing. Anything attached after them (strip tiles
    // re-attached by rebuildStrip(), in particular) would otherwise draw
    // on top — including hiding the overlay behind a tile.
    recreatePresetOverlayOnTop();
  }
  if (mPresetBackdrop) mPresetBackdrop->Hide(!willShow);
  if (mPresetOverlay)  mPresetOverlay ->Hide(!willShow);
  SetDirty(false);
}

void ToneRoot::attachPresetOverlay(bool startVisible, int64_t activeId)
{
  IGraphics* g = GetUI();
  if (!g) return;

  // Click-catcher backdrop FIRST so the overlay z-orders above it. The
  // backdrop covers the whole window when visible; clicking anywhere
  // outside the overlay's footprint hits the backdrop, which closes the
  // overlay (and the backdrop) via togglePresetOverlay.
  mPresetBackdrop = new T3kClickBackdrop(mRECT,
      /*onClick*/ [this] {
        if (mPresetOverlay && !mPresetOverlay->IsHidden())
          togglePresetOverlay();
      });
  g->AttachControl(mPresetBackdrop);
  mPresetBackdrop->Hide(!startVisible);

  mPresetOverlay = new T3kPresetOverlay(IRECT(0.f, 0.f, 1.f, 1.f));

  // Pull the real preset list from PresetStore. PresetStore::list()
  // returns the same shape as the overlay's PresetRow — we just have
  // to convert namespaces (the overlay's PresetRow lives in t3k::ui).
  std::vector<PresetRow> rows;
  for (const auto& r : ::t3k::library::PresetStore::instance().list()) {
    rows.push_back({ r.id, r.name, r.active });
  }
  mPresetOverlay->setPresets(std::move(rows));
  mPresetOverlay->setActiveId(activeId);
  mPresetOverlay->onSelect = [this](int64_t id) {
    this->loadPreset(id);
  };
  mPresetOverlay->onSave     = [this]() { this->saveCurrentPreset(); };
  mPresetOverlay->onSaveAs   = [this]() {
    // Inline name-entry via iPlug2's CreateTextEntry; the completion
    // handler routes through ToneRoot::OnTextEntryCompletion (the
    // overlay forwards its own completion to ToneRoot).
    if (auto* ui = GetUI()) {
      namespace th = ::t3k::theme;
      const IText t = IText(th::kTypeBody, th::kText, th::kFontBody,
                            EAlign::Near, EVAlign::Middle)
                          .WithTEColors(th::kBgSurface, th::kText);
      const IRECT prompt(mPresetPillRect.L,
                         mPresetPillRect.B + t3k::theme::kS2,
                         mPresetPillRect.R,
                         mPresetPillRect.B + t3k::theme::kS2 + 28.f);
      mPendingTextEntry = PendingTextEntry::SaveAs;
      ui->CreateTextEntry(*this, t, prompt, "New preset");
    }
  };
  // Per-row right-click → Rename/Delete popup. The overlay calls this
  // whenever the user right-clicks a preset row; we stash the row id
  // + name and open iPlug2's CreatePopupMenu. OnPopupMenuSelection
  // (overridden below) routes the choice back to PresetStore.
  mPresetOverlay->onRowContextMenu = [this](int64_t id, const std::string& name) {
    if (auto* ui = GetUI()) {
      mPendingPresetMenuId = id;
      mPendingPresetMenuName = name;
      mPendingMenuKind = PendingMenuKind::PresetRowAction;
      // Single static menu; reused. iPlug2 owns its lifetime via the
      // CreatePopupMenu call's reference.
      static iplug::igraphics::IPopupMenu rowMenu;
      rowMenu.Clear();
      rowMenu.AddItem("Rename\xE2\x80\xA6");
      rowMenu.AddItem("Delete\xE2\x80\xA6");
      const IRECT anchor(mPresetPillRect.L, mPresetPillRect.B,
                         mPresetPillRect.R, mPresetPillRect.B + 4.f);
      ui->CreatePopupMenu(*this, rowMenu, anchor);
    }
  };
  mPresetOverlay->onMoreMenu = [this]() {
    // The overflow "⋯" button on the action row now opens the
    // active-preset's context menu (Rename / Delete) — same as
    // right-clicking the active row directly. Pulls active id from
    // PresetStore so a stale overlay state doesn't operate on the
    // wrong row.
    const int64_t active = ::t3k::library::PresetStore::instance().activeId();
    if (active <= 0) return;
    // Resolve name from the current list.
    std::string activeName;
    for (const auto& r : ::t3k::library::PresetStore::instance().list()) {
      if (r.id == active) { activeName = r.name; break; }
    }
    if (mPresetOverlay && mPresetOverlay->onRowContextMenu) {
      mPresetOverlay->onRowContextMenu(active, activeName);
    }
  };
  g->AttachControl(mPresetOverlay);
  mPresetOverlay->Hide(!startVisible);

  // Size to the anchored rect computed in OnResize. If OnResize hasn't run
  // yet (first attach path) the overlay carries the 1×1 placeholder until
  // it does; subsequent paint cycles inherit the proper rect.
  const float top  = mPresetPillRect.B + t3k::theme::kS2;
  const float left = mPresetPillRect.R - kPresetOverlayW;
  mPresetOverlay->SetTargetAndDrawRECTs(
      IRECT(left, top, left + kPresetOverlayW, top + kPresetOverlayH));
}

void ToneRoot::refreshPresetList()
{
  if (!mPresetOverlay) return;
  std::vector<PresetRow> rows;
  for (const auto& r : ::t3k::library::PresetStore::instance().list()) {
    rows.push_back({ r.id, r.name, r.active });
  }
  mPresetOverlay->setPresets(std::move(rows));
  mPresetOverlay->setActiveId(
      ::t3k::library::PresetStore::instance().activeId());
  syncPillToActivePreset();
}

void ToneRoot::saveCurrentPreset()
{
  if (!mToneView) return;
  const auto state = mToneView->snapshotPresetState();
  ::t3k::library::PresetStore::instance().saveCurrent(state);
  if (mPresetPill) mPresetPill->setDirty(false);
  refreshPresetList();
}

void ToneRoot::saveAsPreset(const std::string& name)
{
  if (name.empty() || !mToneView) return;
  const auto state = mToneView->snapshotPresetState();
  const int64_t id = ::t3k::library::PresetStore::instance().saveAs(name, state);
  if (id > 0) {
    ::t3k::library::PresetStore::instance().setActiveId(id);
  }
  if (mPresetPill) mPresetPill->setDirty(false);
  refreshPresetList();
}

void ToneRoot::loadPreset(int64_t presetId)
{
  if (presetId <= 0 || !mToneView) return;
  auto state = ::t3k::library::PresetStore::instance().load(presetId);
  if (!state.has_value()) return;
  // Snapshot BEFORE the apply so undo can roll back to the prior
  // chain. mUndoApplyInProgress suppresses pushUndo during undo()'s
  // own apply (which calls this path indirectly via setActiveId
  // checks — defensive).
  this->pushUndo();
  ::t3k::library::PresetStore::instance().setActiveId(presetId);
  mToneView->applyPresetState(*state);
  if (mPresetPill) mPresetPill->setDirty(false);
  refreshPresetList();
  this->commitUndo();
}

void ToneRoot::syncPillToActivePreset()
{
  if (!mPresetPill || !mPresetOverlay) return;
  const int64_t active = mPresetOverlay->activePresetId();
  for (const auto& p : mPresetOverlay->presets()) {
    if (p.id == active) {
      mPresetPill->setActivePresetName(p.name);
      return;
    }
  }
}

void ToneRoot::OnTextEntryCompletion(const char* str, int /*valIdx*/)
{
  // Polish 3c: two text-entry paths converge here — Save-As… (the
  // overlay's primary "create a new preset" flow) and Rename (the
  // per-row context-menu action). mPendingTextEntry tells us which.
  const PendingTextEntry kind = mPendingTextEntry;
  mPendingTextEntry = PendingTextEntry::None;

  if (!str) return;
  const std::string name(str);
  if (name.empty()) return;

  switch (kind) {
    case PendingTextEntry::SaveAs:
      saveAsPreset(name);
      // Save-As reattaches the overlay-on-top so the new entry shows
      // even if the strip rebuilt during the operation.
      if (mPresetOverlay && !mPresetOverlay->IsHidden()) {
        recreatePresetOverlayOnTop();
      }
      break;
    case PendingTextEntry::RenamePreset:
      if (mPendingPresetMenuId > 0) {
        ::t3k::library::PresetStore::instance().rename(mPendingPresetMenuId, name);
        mPendingPresetMenuId = 0;
        mPendingPresetMenuName.clear();
        refreshPresetList();
      }
      break;
    case PendingTextEntry::None:
      // No active flow — treat as Save-As for backwards-compat with
      // the original PresetOverlay::onSaveAs path that didn't set the
      // pending flag.
      saveAsPreset(name);
      break;
  }
}

void ToneRoot::recreateSettingsModalOnTop()
{
  IGraphics* g = GetUI();
  if (!g) return;
  if (mSettingsModal) {
    mSettingsModal->detachAllChildren();
    g->RemoveControl(mSettingsModal);
    mSettingsModal = nullptr;
  }
  mSettingsModal = new T3kSettingsModal(mRECT,
      [this]() {
        if (mSettingsModal) mSettingsModal->Hide(true);
        SetDirty(false);
      });
  g->AttachControl(mSettingsModal);
  mSettingsModal->Hide(true);
}

void ToneRoot::recreatePresetOverlayOnTop()
{
  IGraphics* g = GetUI();
  if (!g || !mPresetOverlay) return;

  // iPlug2's RemoveControl(IControl*) always frees the underlying control
  // (the iPlug2 in this tree does not expose a "detach without delete"
  // variant — see WDL_PtrList::Delete with wantDelete=false, which is not
  // surfaced through IGraphics's public API). So z-order promotion has to
  // go through destroy-and-recreate; we preserve user-visible state
  // (active preset id) and re-attach with a fresh control.
  //
  // Backdrop is recreated alongside the overlay so the pair stays at the
  // top of the z-order in the right relative order (backdrop, then overlay).
  const int64_t activeId = mPresetOverlay->activePresetId();
  const bool wasVisible = !mPresetOverlay->IsHidden();
  if (mPresetBackdrop) {
    g->RemoveControl(mPresetBackdrop);
    mPresetBackdrop = nullptr;
  }
  g->RemoveControl(mPresetOverlay);
  mPresetOverlay = nullptr;
  attachPresetOverlay(/*startVisible*/ wasVisible, activeId);
}

// ── Phase 5: account-menu wiring ────────────────────────────────

IRECT ToneRoot::accountMenuRect() const
{
  // Anchored right-aligned beneath the avatar/pill rect (same pattern
  // as the preset overlay anchors beneath the preset pill).
  const float top  = mAvatarRect.B + t3k::theme::kS2;
  const float left = mAvatarRect.R - kAccountMenuW;
  return IRECT(left, top, left + kAccountMenuW, top + kAccountMenuH);
}

void ToneRoot::attachAccountMenu(bool startVisible)
{
  IGraphics* g = GetUI();
  if (!g) return;

  // Backdrop FIRST so the menu z-orders above it.
  mAccountBackdrop = new T3kClickBackdrop(mRECT,
      /*onClick*/ [this] {
        if (mAccountMenu && !mAccountMenu->IsHidden()) {
          toggleAccountMenu();
        }
      });
  g->AttachControl(mAccountBackdrop);
  mAccountBackdrop->Hide(!startVisible);

  mAccountMenu = new T3kAccountMenu(accountMenuRect());
  // Same target as the Ctrl+Shift+S global hotkey — shared body lives
  // in openSettings() below.
  mAccountMenu->onSettings = [this]() { this->openSettings(); };
  mAccountMenu->onMockSignIn = [this]() {
    ::t3k::cloud::Session::instance().mockSignIn();
    if (mAccountMenu && !mAccountMenu->IsHidden()) toggleAccountMenu();
  };
  mAccountMenu->onSignOut = [this]() {
    ::t3k::cloud::Session::instance().signOut();
    if (mAccountMenu && !mAccountMenu->IsHidden()) toggleAccountMenu();
  };
  g->AttachControl(mAccountMenu);
  mAccountMenu->Hide(!startVisible);
  refreshAccountMenuItems();
}

void ToneRoot::recreateAccountMenuOnTop()
{
  IGraphics* g = GetUI();
  if (!g || !mAccountMenu) return;

  const bool wasVisible = !mAccountMenu->IsHidden();
  if (mAccountBackdrop) {
    g->RemoveControl(mAccountBackdrop);
    mAccountBackdrop = nullptr;
  }
  g->RemoveControl(mAccountMenu);
  mAccountMenu = nullptr;
  attachAccountMenu(/*startVisible*/ wasVisible);
}

void ToneRoot::toggleAccountMenu()
{
  if (!mAccountMenu) return;
  const bool willShow = mAccountMenu->IsHidden();
  if (willShow) {
    // Push backdrop+menu to the top of the z-order so they draw above
    // any strip tiles re-attached by ToneView::rebuildStrip().
    recreateAccountMenuOnTop();
    refreshAccountMenuItems();
  }
  if (mAccountBackdrop) mAccountBackdrop->Hide(!willShow);
  if (mAccountMenu)     mAccountMenu->Hide(!willShow);
  SetDirty(false);
}

void ToneRoot::refreshAccountMenuItems()
{
  if (!mAccountMenu) return;
  AccountMenuItems items;
  const bool signedIn =
      (::t3k::cloud::Session::instance().state() ==
       ::t3k::cloud::Session::State::SignedIn);

  // Mock sign-in is visible whenever no real OAuth client_id is
  // configured AND the user is signed-out. The original "Debug-only"
  // gate (#ifdef _DEBUG) was unreliable across the user's build
  // configurations — iPlug2's vcxproj doesn't consistently propagate
  // _DEBUG to every cpp. Tying visibility to isConfigured() captures
  // the actual intent: this entry exists *because* the real flow
  // can't run yet. Once kClientId is swapped in OAuthConfig.h, the
  // entry disappears automatically.
  items.showMockSignIn = !signedIn && !::t3k::cloud::OAuthFlow::isConfigured();
  items.showSignOut    = signedIn;
  if (signedIn) {
    if (auto u = ::t3k::cloud::Session::instance().currentUser();
        u.has_value()) {
      items.activeUsername = u->username;
    }
  }
  mAccountMenu->setItems(items);
}

void ToneRoot::onSessionEvent(const ::t3k::cloud::SessionEvent& ev)
{
  using K = ::t3k::cloud::SessionEvent::Kind;
  switch (ev.kind) {
    case K::SignInStarted:
      // No-op for Phase 5 UI — the browser is already up; the user
      // doesn't need a spinner.
      break;
    case K::SignedIn:
      mSignedIn = true;
      if (mSignInPill) mSignInPill->Hide(true);
      refreshAccountMenuItems();
      SetDirty(false);
      break;
    case K::SignedOut:
    case K::SessionExpired:
      mSignedIn = false;
      if (mSignInPill) mSignInPill->Hide(false);
      refreshAccountMenuItems();
      SetDirty(false);
      break;
    case K::SignInFailed:
      // Toast the error so the user knows why the browser didn't lead
      // anywhere (the most common case is "OAuth client_id not
      // configured" which only happens to fork operators who haven't
      // set up their app yet).
      setSignInStatus(ev.error_message.empty()
                          ? std::string("Sign-in failed")
                          : "Sign-in: " + ev.error_message);
      break;
  }
}

void ToneRoot::setSignInStatus(const std::string& msg, int durationMs)
{
  std::lock_guard<std::mutex> lk(mSignInStatusMtx);
  mSignInStatus = msg;
  mSignInStatusExpiry = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(durationMs);
  // SetDirty(false) — safe to call from any thread per iPlug2's
  // tolerance for the dirty flag. The next paint cycle picks it up.
  SetDirty(false);
}

// ── Polish 3c: downloads popover ────────────────────────────────

void ToneRoot::toggleDownloadsPopover()
{
  if (!mDownloadsPopover) return;
  const bool willShow = mDownloadsPopover->IsHidden();
  if (willShow) {
    // Same z-order discipline as the preset overlay — push the
    // backdrop+popover to the end of mControls so cards/tiles don't
    // bury them.
    IGraphics* g = GetUI();
    if (g) {
      if (mDownloadsBackdrop) {
        g->RemoveControl(mDownloadsBackdrop);
        mDownloadsBackdrop = nullptr;
      }
      g->RemoveControl(mDownloadsPopover);
      mDownloadsPopover = nullptr;

      mDownloadsBackdrop = new T3kClickBackdrop(mRECT,
          /*onClick*/ [this] {
            if (mDownloadsPopover && !mDownloadsPopover->IsHidden())
              toggleDownloadsPopover();
          });
      g->AttachControl(mDownloadsBackdrop);
      mDownloadsBackdrop->Hide(false);

      mDownloadsPopover = new T3kDownloadsPopover(
          IRECT(0.f, 0.f, 1.f, 1.f),
          [this]() {
            return mDownloadsPill ? mDownloadsPill->snapshotRows()
                                  : std::vector<T3kDownloadsPill::Row>{};
          });
      g->AttachControl(mDownloadsPopover);
      // Size to anchored rect — right-aligned with pill so the popover
      // (which is wider than the pill) stays inside the window edge.
      const float top  = mDownloadsPillRect.B + t3k::theme::kS2;
      const float left = mDownloadsPillRect.R - kDownloadsPopoverW;
      mDownloadsPopover->SetTargetAndDrawRECTs(
          IRECT(left, top, left + kDownloadsPopoverW, top + kDownloadsPopoverH));
      mDownloadsPopover->Hide(false);
    }
  } else {
    if (mDownloadsBackdrop) mDownloadsBackdrop->Hide(true);
    if (mDownloadsPopover)  mDownloadsPopover->Hide(true);
  }
  SetDirty(false);
}

// ── Polish 3c: undo / redo ─────────────────────────────────────

void ToneRoot::pushUndo()
{
  if (mUndoApplyInProgress) return;
  if (!mToneView) return;
  UndoEntry e;
  e.before = mToneView->snapshotPresetState();
  // "after" gets filled in commitUndo. If commit never runs (the
  // mutation aborted), the partial entry still rolls back correctly
  // because undo() only consumes `before`.
  mUndoStack.push_back(std::move(e));
  // A new branch invalidates any redo.
  mRedoStack.clear();
  updateUndoGlyphs();
}

void ToneRoot::commitUndo()
{
  if (mUndoApplyInProgress) return;
  if (mUndoStack.empty() || !mToneView) return;
  mUndoStack.back().after = mToneView->snapshotPresetState();
  updateUndoGlyphs();
}

void ToneRoot::undo()
{
  if (mUndoStack.empty() || !mToneView) return;
  UndoEntry e = std::move(mUndoStack.back());
  mUndoStack.pop_back();
  mUndoApplyInProgress = true;
  mToneView->applyPresetState(e.before);
  mUndoApplyInProgress = false;
  mRedoStack.push_back(std::move(e));
  if (mPresetPill) mPresetPill->setDirty(true);
  updateUndoGlyphs();
  SetDirty(false);
}

void ToneRoot::redo()
{
  if (mRedoStack.empty() || !mToneView) return;
  UndoEntry e = std::move(mRedoStack.back());
  mRedoStack.pop_back();
  mUndoApplyInProgress = true;
  mToneView->applyPresetState(e.after);
  mUndoApplyInProgress = false;
  mUndoStack.push_back(std::move(e));
  if (mPresetPill) mPresetPill->setDirty(true);
  updateUndoGlyphs();
  SetDirty(false);
}

void ToneRoot::updateUndoGlyphs()
{
  if (mUndoGlyph) mUndoGlyph->setDisabled(mUndoStack.empty());
  if (mRedoGlyph) mRedoGlyph->setDisabled(mRedoStack.empty());
}

// ── Polish 3c: popup menus from preset overlay ─────────────────

void ToneRoot::OnPopupMenuSelection(iplug::igraphics::IPopupMenu* pSelectedMenu,
                                    int /*valIdx*/)
{
  if (!pSelectedMenu) {
    mPendingMenuKind = PendingMenuKind::None;
    return;
  }
  iplug::igraphics::IPopupMenu::Item* item = pSelectedMenu->GetChosenItem();
  if (!item) {
    mPendingMenuKind = PendingMenuKind::None;
    return;
  }
  const int idx = pSelectedMenu->GetChosenItemIdx();

  switch (mPendingMenuKind) {
    case PendingMenuKind::PresetRowAction: {
      mPendingMenuKind = PendingMenuKind::None;
      // 0 = Rename, 1 = Delete (matches the AddItem order in
      // onRowContextMenu).
      if (idx == 0) {
        // Rename → prompt for new name.
        if (auto* ui = GetUI()) {
          namespace th = ::t3k::theme;
          const IText t = IText(th::kTypeBody, th::kText, th::kFontBody,
                                EAlign::Near, EVAlign::Middle)
                              .WithTEColors(th::kBgSurface, th::kText);
          const IRECT prompt(mPresetPillRect.L,
                             mPresetPillRect.B + t3k::theme::kS2,
                             mPresetPillRect.R,
                             mPresetPillRect.B + t3k::theme::kS2 + 28.f);
          mPendingTextEntry = PendingTextEntry::RenamePreset;
          ui->CreateTextEntry(*this, t, prompt,
                              mPendingPresetMenuName.c_str());
        }
      } else if (idx == 1) {
        // Delete → confirm step. We need to open a second popup, but
        // doing so directly here is unsafe: iPlug2's
        // SetControlValueAfterPopupMenu reads mInPopupMenu BOTH at
        // entry AND again after dispatching this handler; a nested
        // CreatePopupMenu sets mInPopupMenu=nullptr on its way out
        // and the outer frame then dereferences null -> host crash
        // (observed in Ableton). Defer the confirm-popup creation
        // to the next idle tick so the outer popup-menu callback
        // chain has fully unwound before we open a new menu. The
        // preset id + name are already in mPendingPresetMenuId/Name
        // and stay populated until the deferred handler consumes
        // them.
        mPendingDeferredAction = DeferredAction::OpenDeleteConfirm;
      }
      break;
    }
    case PendingMenuKind::PresetDeleteConfirm: {
      mPendingMenuKind = PendingMenuKind::None;
      if (idx == 0 && mPendingPresetMenuId > 0) {
        ::t3k::library::PresetStore::instance().remove(mPendingPresetMenuId);
        mPendingPresetMenuId = 0;
        mPendingPresetMenuName.clear();
        refreshPresetList();
      }
      break;
    }
    case PendingMenuKind::None:
    default:
      break;
  }
}

void ToneRoot::openSettings()
{
  // Phase 10 — real settings modal. Close the account menu first so
  // it doesn't paint above the modal's dim backdrop.
  if (mAccountMenu && !mAccountMenu->IsHidden()) toggleAccountMenu();
  // Push to top of z-order — Cloud / Library cards attach lazily
  // after OnAttached, so a modal attached during OnAttached gets
  // buried beneath them and the user sees nothing.
  recreateSettingsModalOnTop();
  if (mSettingsModal) {
    mSettingsModal->refresh();
    mSettingsModal->Hide(false);
    SetDirty(false);
  }
}

void ToneRoot::OnGUIIdle()
{
  // 2026-05-25 — drain the deferred-action queue. See the comment on
  // DeferredAction in ToneRoot.h for why opening a nested popup
  // directly from OnPopupMenuSelection crashes the host. iPlug2
  // calls OnGUIIdle when the editor has been quiet for ~10 frames,
  // which is plenty of time for the outer popup-menu callback chain
  // to have fully unwound.
  if (mPendingDeferredAction == DeferredAction::OpenDeleteConfirm) {
    mPendingDeferredAction = DeferredAction::None;

    if (mPendingPresetMenuId > 0) {
      if (auto* ui = GetUI()) {
        static iplug::igraphics::IPopupMenu confirmMenu;
        confirmMenu.Clear();
        const std::string yes =
            "Yes, delete \"" + mPendingPresetMenuName + "\"";
        confirmMenu.AddItem(yes.c_str());
        confirmMenu.AddItem("Cancel");
        mPendingMenuKind = PendingMenuKind::PresetDeleteConfirm;
        const IRECT anchor(mPresetPillRect.L, mPresetPillRect.B,
                           mPresetPillRect.R, mPresetPillRect.B + 4.f);
        ui->CreatePopupMenu(*this, confirmMenu, anchor);
      }
    }
  }
}

}  // namespace t3k::ui
