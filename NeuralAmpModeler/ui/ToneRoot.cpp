// ToneRoot.cpp — see ToneRoot.h for an overview of responsibilities.

#include "ToneRoot.h"

#include <algorithm>
#include <vector>

#include "theme.h"
#include "layout.h"

// Child controls + views.
#include "controls/T3kButton.h"
#include "controls/T3kKnob.h"
#include "controls/T3kLogo.h"
#include "controls/T3kSearchBar.h"
#include "controls/T3kTabBar.h"
#include "views/AmpView.h"
#include "views/CloudView.h"
#include "views/DownloadsView.h"
#include "views/IRView.h"
#include "views/LibraryView.h"
#include "views/SettingsView.h"

// Plug-in header — gives us the EParams enum (kInputLevel, kToneBass, ...).
#include "../NeuralAmpModeler.h"

namespace t3k::ui {

using namespace iplug::igraphics;

namespace {
// Layout constants — pulled from theme where reasonable; these are
// dimensions specific to ToneRoot's chrome that don't belong in theme.h.
constexpr float kHeaderH   = 52.f;
constexpr float kTabStripH = 32.f;
constexpr float kKnobRowH  = 84.f;
}  // namespace

ToneRoot::ToneRoot(const IRECT& bounds, NeuralAmpModeler& plugin)
  : IControl(bounds), mPlugin(plugin)
{
  // Compute initial sub-rects so OnAttached can size children correctly.
  OnResize();
}

void ToneRoot::OnResize()
{
  // Divide the window vertically into Header / Tabs / Body / KnobRow.
  auto bottomSplit = t3k::layout::rowFixedBottom(mRECT, kKnobRowH);
  IRECT topRegion = bottomSplit.first;
  mKnobRowRect = bottomSplit.second;

  auto headerSplit = t3k::layout::rowFixedTop(topRegion, kHeaderH);
  mHeaderRect = headerSplit.first;
  IRECT belowHeader = headerSplit.second;

  auto tabSplit = t3k::layout::rowFixedTop(belowHeader, kTabStripH);
  mTabStripRect = tabSplit.first;
  mBodyRect = tabSplit.second;

  // Header sub-rects via row weights {logo, search, downloads, avatar}.
  // Weights chosen to roughly match the mockup's proportions.
  auto headerCells = t3k::layout::row(
    t3k::layout::pad(mHeaderRect, t3k::theme::kS2, t3k::theme::kS4,
                                  t3k::theme::kS2, t3k::theme::kS4),
    /*weights*/ { 0.18f, 1.0f, 0.12f, 0.06f },
    /*gap*/     t3k::theme::kS3);

  mLogoRect      = headerCells[0];
  mSearchRect    = headerCells[1];
  mDownloadsRect = headerCells[2];
  mAvatarRect    = headerCells[3];

  // Resize header children if they already exist.
  if (mLogo)          mLogo->SetTargetAndDrawRECTs(mLogoRect);
  if (mSearchBar)     mSearchBar->SetTargetAndDrawRECTs(mSearchRect);
  if (mDownloadsPill) mDownloadsPill->SetTargetAndDrawRECTs(mDownloadsRect);
  if (mTabBar) {
    mTabBar->SetTargetAndDrawRECTs(
      t3k::layout::pad(mTabStripRect, 0.f, t3k::theme::kS4,
                                      0.f, t3k::theme::kS4));
  }

  // Knob row — 5 evenly-spaced cells inside the bottom strip.
  auto knobCells = t3k::layout::row(
    t3k::layout::pad(mKnobRowRect, t3k::theme::kS3, t3k::theme::kS5,
                                   t3k::theme::kS3, t3k::theme::kS5),
    /*weights*/ { 1.f, 1.f, 1.f, 1.f, 1.f },
    /*gap*/     t3k::theme::kS4);

  if (mKnobIn)     mKnobIn->SetTargetAndDrawRECTs(knobCells[0]);
  if (mKnobBass)   mKnobBass->SetTargetAndDrawRECTs(knobCells[1]);
  if (mKnobMid)    mKnobMid->SetTargetAndDrawRECTs(knobCells[2]);
  if (mKnobTreble) mKnobTreble->SetTargetAndDrawRECTs(knobCells[3]);
  if (mKnobOut)    mKnobOut->SetTargetAndDrawRECTs(knobCells[4]);

  // Resize all four body views; only the active one is visible.
  if (mAmpView)     mAmpView->SetTargetAndDrawRECTs(mBodyRect);
  if (mIRView)      mIRView->SetTargetAndDrawRECTs(mBodyRect);
  if (mCloudView)   mCloudView->SetTargetAndDrawRECTs(mBodyRect);
  if (mLibraryView) mLibraryView->SetTargetAndDrawRECTs(mBodyRect);

  // Overlay rects — positioned beneath their trigger pills.
  if (mDownloadsView) {
    IRECT r(mDownloadsRect.L - 80.f,
            mDownloadsRect.B + t3k::theme::kS2,
            mDownloadsRect.R + 80.f,
            mDownloadsRect.B + t3k::theme::kS2 + 200.f);
    mDownloadsView->SetTargetAndDrawRECTs(r);
  }
  if (mSettingsView) {
    IRECT r(mAvatarRect.L - 220.f,
            mAvatarRect.B + t3k::theme::kS2,
            mAvatarRect.R,
            mAvatarRect.B + t3k::theme::kS2 + 140.f);
    mSettingsView->SetTargetAndDrawRECTs(r);
  }
}

void ToneRoot::OnAttached()
{
  IGraphics* g = GetUI();
  if (!g) return;

  // --- Header ---
  mLogo = new T3kLogo(mLogoRect);
  g->AttachControl(mLogo);

  mSearchBar = new T3kSearchBar(mSearchRect,
    /*onChanged*/ [](const std::string& /*text*/) {
      // Phase 6 wires this to the Cloud tab's debounced search.
    },
    /*placeholder*/ "Search\xE2\x80\xA6");
  g->AttachControl(mSearchBar);

  mDownloadsPill = new T3kButton(mDownloadsRect, "Downloads",
    /*onClick*/ [this]() { this->toggleDownloadsOverlay(); },
    T3kButton::Variant::Secondary);
  g->AttachControl(mDownloadsPill);

  // --- Tab strip ---
  mTabBar = new T3kTabBar(
    t3k::layout::pad(mTabStripRect, 0.f, t3k::theme::kS4,
                                    0.f, t3k::theme::kS4),
    /*tabs*/ { "Amp", "Cabinet / IR", "Cloud", "Library" },
    /*onChanged*/ [this](int idx) { this->switchTab(static_cast<Tab>(idx)); },
    /*initial*/ static_cast<int>(Tab::Amp));
  g->AttachControl(mTabBar);

  // --- Knob row (persistent across tabs) ---
  // Param indices come from NeuralAmpModeler.h's EParams enum.
  auto knobCells = t3k::layout::row(
    t3k::layout::pad(mKnobRowRect, t3k::theme::kS3, t3k::theme::kS5,
                                   t3k::theme::kS3, t3k::theme::kS5),
    { 1.f, 1.f, 1.f, 1.f, 1.f }, t3k::theme::kS4);

  mKnobIn     = new T3kKnob(knobCells[0], ::kInputLevel,  "INPUT");
  mKnobBass   = new T3kKnob(knobCells[1], ::kToneBass,    "BASS");
  mKnobMid    = new T3kKnob(knobCells[2], ::kToneMid,     "MID");
  mKnobTreble = new T3kKnob(knobCells[3], ::kToneTreble,  "TREBLE");
  mKnobOut    = new T3kKnob(knobCells[4], ::kOutputLevel, "OUTPUT");
  g->AttachControl(mKnobIn);
  g->AttachControl(mKnobBass);
  g->AttachControl(mKnobMid);
  g->AttachControl(mKnobTreble);
  g->AttachControl(mKnobOut);

  // --- Tab body views (all created; non-active ones hidden) ---
  mAmpView     = new AmpView(mBodyRect);
  mIRView      = new IRView(mBodyRect);
  mCloudView   = new CloudView(mBodyRect);
  mLibraryView = new LibraryView(mBodyRect);
  g->AttachControl(mAmpView);
  g->AttachControl(mIRView);
  g->AttachControl(mCloudView);
  g->AttachControl(mLibraryView);
  // Show the initial tab; hide the rest.
  mIRView->Hide(true);
  mCloudView->Hide(true);
  mLibraryView->Hide(true);

  // --- Overlays (hidden by default) ---
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
  // The plug-in's panel background is solid black via AttachPanelBackground.
  // We draw the avatar circle + initials here since it has no dedicated
  // control. (When Phase 5 lands signed-in user state, the avatar may
  // become a T3kButton.)

  // Avatar — filled circle with a subtle border and white "KV" initials.
  const IRECT& a = mAvatarRect;
  const float cx = a.MW();
  const float cy = a.MH();
  const float r  = std::min(a.W(), a.H()) * 0.5f - 2.f;

  g.FillCircle(t3k::theme::kBgSurface, cx, cy, r);
  g.DrawCircle(t3k::theme::kBorder, cx, cy, r, nullptr, 1.f);
  g.DrawText(IText(t3k::theme::kTypeSmall, t3k::theme::kTextMuted,
                   t3k::theme::kFontBodyMed, EAlign::Center, EVAlign::Middle),
             "KV",
             IRECT(cx - r, cy - r, cx + r, cy + r));

  // Subtle 1px separators below the header and above the knob row.
  g.FillRect(t3k::theme::kBorder,
             IRECT(mRECT.L, mHeaderRect.B - 1.f, mRECT.R, mHeaderRect.B));
  g.FillRect(t3k::theme::kBorder,
             IRECT(mRECT.L, mKnobRowRect.T, mRECT.R, mKnobRowRect.T + 1.f));
}

void ToneRoot::switchTab(Tab tab)
{
  if (tab == mActiveTab) return;
  hideAllBodies();
  mActiveTab = tab;
  IControl* next = nullptr;
  switch (tab) {
    case Tab::Amp:     next = mAmpView;     break;
    case Tab::IR:      next = mIRView;      break;
    case Tab::Cloud:   next = mCloudView;   break;
    case Tab::Library: next = mLibraryView; break;
    default: break;
  }
  if (next) next->Hide(false);
}

void ToneRoot::hideAllBodies()
{
  if (mAmpView)     mAmpView->Hide(true);
  if (mIRView)      mIRView->Hide(true);
  if (mCloudView)   mCloudView->Hide(true);
  if (mLibraryView) mLibraryView->Hide(true);
}

void ToneRoot::toggleDownloadsOverlay()
{
  mDownloadsOpen = !mDownloadsOpen;
  // Mutually exclusive with the settings overlay.
  if (mSettingsOpen) {
    mSettingsOpen = false;
    if (mSettingsView) mSettingsView->Hide(true);
  }
  if (mDownloadsView) {
    mDownloadsView->Hide(!mDownloadsOpen);
    OnResize();  // recompute the overlay rect now that it's visible
  }
}

void ToneRoot::toggleSettingsOverlay()
{
  mSettingsOpen = !mSettingsOpen;
  if (mDownloadsOpen) {
    mDownloadsOpen = false;
    if (mDownloadsView) mDownloadsView->Hide(true);
  }
  if (mSettingsView) {
    mSettingsView->Hide(!mSettingsOpen);
    OnResize();
  }
}

}  // namespace t3k::ui
