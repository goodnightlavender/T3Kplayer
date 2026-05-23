// ToneRoot.cpp — see ToneRoot.h for an overview of responsibilities.

#include "ToneRoot.h"

#include <algorithm>
#include <vector>

#include "theme.h"
#include "layout.h"
#include "../config.h"  // ICON_UNDO_FN, ICON_REDO_FN

// Child controls + views.
#include "controls/T3kLogo.h"
#include "controls/T3kLooseGlyph.h"
#include "controls/T3kPresetOverlay.h"
#include "controls/T3kPresetPill.h"
#include "controls/T3kTabBar.h"
#include "views/CloudView.h"
#include "views/DownloadsView.h"
#include "views/LibraryView.h"
#include "views/SettingsView.h"
#include "views/T3kFirstRunModal.h"
#include "views/ToneView.h"

#include "../library/LibraryScanner.h"
#include "../library/PresetStore.h"
#include "../library/PresetState.h"
#include "../settings/Settings.h"

namespace t3k::ui {

using namespace iplug::igraphics;

namespace {

// Layout constants — single 44px header row (Phase 2b v6).
constexpr float kHeaderH        = 44.f;
constexpr float kLogoW          = 110.f;
constexpr float kLooseW         = 18.f;
constexpr float kLooseGap       = 4.f;
constexpr float kPresetPillW    = 160.f;
constexpr float kPresetPillH    = 24.f;
constexpr float kAvatarW        = 28.f;
constexpr float kPresetGap      = 10.f;  // between preset pill and avatar
constexpr float kPresetOverlayW = 280.f;
constexpr float kPresetOverlayH = 210.f;

}  // namespace

ToneRoot::ToneRoot(const IRECT& bounds, NeuralAmpModeler& plugin)
  : IControl(bounds), mPlugin(plugin)
{
  // Compute initial sub-rects so OnAttached can size children correctly.
  OnResize();
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

  // Center cluster: tab bar fills the space between the loose-glyphs and
  // the preset pill, but the T3kTabBar centers its own labels inside.
  const float tabL = mRedoRect.R + t3k::theme::kS3;
  const float tabR = mPresetPillRect.L - t3k::theme::kS3;
  mTabStripRect = IRECT(tabL, headerPad.T, tabR, headerPad.B);

  // Resize header children if they already exist.
  if (mLogo)       mLogo->SetTargetAndDrawRECTs(mLogoRect);
  if (mUndoGlyph)  mUndoGlyph->SetTargetAndDrawRECTs(mUndoRect);
  if (mRedoGlyph)  mRedoGlyph->SetTargetAndDrawRECTs(mRedoRect);
  if (mTabBar)     mTabBar->SetTargetAndDrawRECTs(mTabStripRect);
  if (mPresetPill) mPresetPill->SetTargetAndDrawRECTs(mPresetPillRect);

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

  // Legacy overlays — hidden; bounded to avoid 0-sized rects.
  if (mDownloadsView) mDownloadsView->SetTargetAndDrawRECTs(IRECT(0.f, 0.f, 1.f, 1.f));
  if (mSettingsView)  mSettingsView->SetTargetAndDrawRECTs(IRECT(0.f, 0.f, 1.f, 1.f));

  // First-run modal covers the entire window.
  if (mFirstRunModal) mFirstRunModal->SetTargetAndDrawRECTs(mRECT);
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
                                 /*onClick*/ []() {},
                                 /*disabled*/ false);
  mRedoGlyph = new T3kLooseGlyph(mRedoRect, ICON_REDO_FN,
                                 /*onClick*/ []() {},
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
        // -1 → ToneView::loadModelIntoSlot picks the first free pedal
        // slot. The user can still drop the model into a specific
        // slot by selecting that slot first (Phase 3.5 wiring).
        mToneView->loadModelIntoSlot(-1, toneId, modelId);
        switchTab(Tab::Tone);
        if (mPresetPill) mPresetPill->setDirty(true);
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

  // ── Legacy overlays — hidden; no longer reachable from the v6 header.
  // Attach BEFORE the preset overlay so the latter stays at the very end
  // of the IGraphics control list (highest z-order). These two never show.
  mDownloadsView = new DownloadsView(IRECT(0.f, 0.f, 1.f, 1.f));
  mSettingsView  = new SettingsView(IRECT(0.f, 0.f, 1.f, 1.f));
  g->AttachControl(mDownloadsView);
  g->AttachControl(mSettingsView);
  mDownloadsView->Hide(true);
  mSettingsView->Hide(true);

  // ── Preset overlay — created hidden, must be attached last ────
  attachPresetOverlay(/*startVisible*/ false,
                      /*activeId*/ ::t3k::library::PresetStore::instance().activeId());

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
  // control. (When Phase 5 lands signed-in user state, the avatar may
  // become a T3kButton.)
  const IRECT& a = mAvatarRect;
  const float cx = a.MW();
  const float cy = a.MH();
  const float r  = std::min(a.W(), a.H()) * 0.5f - 2.f;

  g.FillCircle(th::kBgSurface, cx, cy, r);
  g.DrawCircle(th::kBorder, cx, cy, r, nullptr, 1.f);
  g.DrawText(IText(th::kTypeSmall, th::kTextMuted,
                   th::kFontBodyMed, EAlign::Center, EVAlign::Middle),
             "KV",
             IRECT(cx - r, cy - r, cx + r, cy + r));

  // 1px separator below the header row.
  g.FillRect(th::kBorder,
             IRECT(mRECT.L, mHeaderRect.B - 1.f, mRECT.R, mHeaderRect.B));
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
    // Ensure the overlay is the last (top-most) control before showing.
    // Anything attached after the overlay (the strip tiles, in particular,
    // if rebuildStrip() ran post-OnAttached) would otherwise draw on top.
    recreatePresetOverlayOnTop();
  }
  if (mPresetOverlay) mPresetOverlay->Hide(!willShow);
  SetDirty(false);
}

void ToneRoot::attachPresetOverlay(bool startVisible, int64_t activeId)
{
  IGraphics* g = GetUI();
  if (!g) return;

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
    // The overlay doesn't (yet) own an inline name-entry surface, so
    // prompt via iPlug2's CreateTextEntry. The completion handler on
    // ToneRoot is plumbed through the preset overlay's own
    // OnTextEntryCompletion, which routes to a fresh saveAs.
    if (auto* ui = GetUI()) {
      namespace th = ::t3k::theme;
      // Dark text-entry bg/fg so the system field doesn't flash white.
      const IText t = IText(th::kTypeBody, th::kText, th::kFontBody,
                            EAlign::Near, EVAlign::Middle)
                          .WithTEColors(th::kBgSurface, th::kText);
      // Anchor the prompt over the overlay center so it has a known
      // location regardless of overlay size.
      const IRECT prompt(mPresetPillRect.L,
                         mPresetPillRect.B + t3k::theme::kS2,
                         mPresetPillRect.R,
                         mPresetPillRect.B + t3k::theme::kS2 + 28.f);
      ui->CreateTextEntry(*this, t, prompt, "New preset");
    }
  };
  mPresetOverlay->onMoreMenu = [this]() {
    // Phase 3 ships a minimal More menu: Rename + Delete the active
    // preset. The pop-up uses iPlug2's CreatePopupMenu, and the
    // selection is handled in ToneRoot::OnPopupMenuSelection (not yet
    // overridden — see TODO). For Phase 3 we leave this as a stub and
    // surface a comment so the test plan can catch it.
    //
    // TODO(Phase 3.5): wire ToneRoot::OnPopupMenuSelection to call
    // PresetStore::rename / remove.
    (void)this;
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
  ::t3k::library::PresetStore::instance().setActiveId(presetId);
  mToneView->applyPresetState(*state);
  if (mPresetPill) mPresetPill->setDirty(false);
  refreshPresetList();
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
  // Phase 3: only the Save-As… overlay routes through here. Empty /
  // null str → cancelled.
  if (!str) return;
  const std::string name(str);
  if (name.empty()) return;
  saveAsPreset(name);
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
  const int64_t activeId = mPresetOverlay->activePresetId();
  const bool wasVisible = !mPresetOverlay->IsHidden();
  g->RemoveControl(mPresetOverlay);
  mPresetOverlay = nullptr;
  attachPresetOverlay(/*startVisible*/ wasVisible, activeId);
}

}  // namespace t3k::ui
