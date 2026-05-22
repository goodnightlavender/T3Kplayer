// ToneRoot.cpp — see ToneRoot.h for an overview of responsibilities.

#include "ToneRoot.h"

#include <algorithm>
#include <vector>

#include "theme.h"
#include "layout.h"

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
#include "views/ToneView.h"

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
}

void ToneRoot::OnAttached()
{
  IGraphics* g = GetUI();
  if (!g) return;

  // ── Header ───────────────────────────────────────────────────────
  mLogo = new T3kLogo(mLogoRect);
  g->AttachControl(mLogo);

  // Loose ↶ / ↷ glyphs. Undo enabled, Redo disabled by default (matches
  // the v6 mockup state). Phase 2b stubs the click handlers — Phase 3
  // wires the real undo stack.
  mUndoGlyph = new T3kLooseGlyph(mUndoRect, "\xE2\x86\xB6",
                                 /*onClick*/ []() {},
                                 /*disabled*/ false);
  mRedoGlyph = new T3kLooseGlyph(mRedoRect, "\xE2\x86\xB7",
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
  mPresetPill->setActivePresetName("Default Setting");
  // Phase 2b demo: mark dirty to show the amber dot from the v6 mockup.
  mPresetPill->setDirty(true);
  g->AttachControl(mPresetPill);

  // ── Tab body views ──────────────────────────────────────────────
  mToneView    = new ToneView(mBodyRect, mPlugin);
  mLibraryView = new LibraryView(mBodyRect);
  mCloudView   = new CloudView(mBodyRect);
  g->AttachControl(mToneView);
  g->AttachControl(mLibraryView);
  g->AttachControl(mCloudView);
  // Show only the initial tab.
  mLibraryView->Hide(true);
  mCloudView->Hide(true);

  // ── Preset overlay — created hidden ────────────────────────────
  mPresetOverlay = new T3kPresetOverlay(IRECT(0.f, 0.f, 1.f, 1.f));
  std::vector<PresetRow> demo = {
      { 1, "Default Setting", true  },
      { 2, "My Rhythm Tone",  false },
      { 3, "Lead Tone",       false },
      { 4, "Clean Sparkle",   false },
      { 5, "Heavy Metal Rig", false },
  };
  mPresetOverlay->setPresets(std::move(demo));
  mPresetOverlay->onSelect = [this](int64_t id) {
    if (!mPresetPill || !mPresetOverlay) return;
    for (const auto& p : mPresetOverlay->presets()) {
      if (p.id == id) {
        mPresetPill->setActivePresetName(p.name);
        // Selecting a preset clears the dirty marker.
        mPresetPill->setDirty(false);
        break;
      }
    }
  };
  // Save/SaveAs/More are stubs until Phase 3 wires real preset persistence.
  mPresetOverlay->onSave     = []() {};
  mPresetOverlay->onSaveAs   = []() {};
  mPresetOverlay->onMoreMenu = []() {};
  g->AttachControl(mPresetOverlay);
  mPresetOverlay->Hide(true);

  // ── Legacy overlays — hidden; no longer reachable from the v6 header.
  mDownloadsView = new DownloadsView(IRECT(0.f, 0.f, 1.f, 1.f));
  mSettingsView  = new SettingsView(IRECT(0.f, 0.f, 1.f, 1.f));
  g->AttachControl(mDownloadsView);
  g->AttachControl(mSettingsView);
  mDownloadsView->Hide(true);
  mSettingsView->Hide(true);

  // Resize all children to their proper bounds now that they exist.
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
  mPresetOverlay->Hide(!mPresetOverlay->IsHidden());
  SetDirty(false);
}

}  // namespace t3k::ui
