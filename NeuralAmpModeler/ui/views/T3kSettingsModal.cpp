// T3kSettingsModal.cpp — see T3kSettingsModal.h.

#include "T3kSettingsModal.h"

#include <cstdio>
#include <cstring>

#include "IGraphics.h"
#include "wdlstring.h"

#include "../theme.h"
#include "../controls/T3kButton.h"
#include "../../cloud/Session.h"
#include "../../cloud/SyncConfig.h"
#include "../../library/LibraryScanner.h"
#include "../../settings/Settings.h"
#include "../../config.h"  // PLUG_NAME, PLUG_VERSION_STR, PLUG_COPYRIGHT_STR

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

constexpr float kCardW   = 640.f;
constexpr float kCardH   = 420.f;
constexpr float kBtnH    = 32.f;
constexpr float kRowH    = 56.f;     // each labeled row in the body

// Backdrop alpha — match T3kRestoreModal so the visual identity is
// consistent across plug-in modals.
const IColor kBackdrop {153, 0, 0, 0};  // ~60% black

}  // namespace

T3kSettingsModal::T3kSettingsModal(const IRECT& bounds, OnClose onClose)
: IControl(bounds)
, mOnClose(std::move(onClose))
{
  OnResize();
  refresh();
}

void T3kSettingsModal::OnResize()
{
  const float cx = mRECT.MW();
  const float cy = mRECT.MH();
  mCardRect = IRECT(cx - kCardW * 0.5f, cy - kCardH * 0.5f,
                    cx + kCardW * 0.5f, cy + kCardH * 0.5f);

  namespace th = ::t3k::theme;

  const float rowL = mCardRect.L + th::kS5;
  const float rowR = mCardRect.R - th::kS5;
  const float bw   = 120.f;

  // Row 1: TONE3000 root + CHANGE… (right-aligned).
  float rowTop = mCardRect.T + th::kS5 + th::kTypeH1 + th::kS5;
  const float btnY1 = rowTop + (kRowH - kBtnH) * 0.5f;
  mChangeRootBtnRect = IRECT(rowR - bw, btnY1, rowR, btnY1 + kBtnH);

  // Row 2: REFRESH LIBRARY (left-aligned).
  rowTop += kRowH;
  const float btnY2 = rowTop + (kRowH - kBtnH) * 0.5f;
  mRescanBtnRect = IRECT(rowL, btnY2, rowL + 180.f, btnY2 + kBtnH);

  // Row 3: SIGN OUT (left-aligned).
  rowTop += kRowH;
  const float btnY3 = rowTop + (kRowH - kBtnH) * 0.5f;
  mSignOutBtnRect = IRECT(rowL, btnY3, rowL + 180.f, btnY3 + kBtnH);

  // CLOSE — bottom-right of the card.
  const float closeBtnW = 120.f;
  const float closeBtnY = mCardRect.B - th::kS5 - kBtnH;
  mCloseBtnRect = IRECT(mCardRect.R - th::kS5 - closeBtnW, closeBtnY,
                        mCardRect.R - th::kS5,             closeBtnY + kBtnH);

  if (mChangeRootBtn) mChangeRootBtn->SetTargetAndDrawRECTs(mChangeRootBtnRect);
  if (mRescanBtn)     mRescanBtn    ->SetTargetAndDrawRECTs(mRescanBtnRect);
  if (mSignOutBtn)    mSignOutBtn   ->SetTargetAndDrawRECTs(mSignOutBtnRect);
  if (mCloseBtn)      mCloseBtn     ->SetTargetAndDrawRECTs(mCloseBtnRect);
}

void T3kSettingsModal::OnAttached()
{
  IGraphics* g = GetUI();
  if (!g) return;

  mChangeRootBtn = new T3kButton(
      mChangeRootBtnRect, "CHANGE\xE2\x80\xA6",
      [this]() { this->pickToneRoot(); },
      T3kButton::Variant::Secondary);
  g->AttachControl(mChangeRootBtn);

  mRescanBtn = new T3kButton(
      mRescanBtnRect, "REFRESH LIBRARY",
      [this]() { this->doRescan(); },
      T3kButton::Variant::Secondary);
  g->AttachControl(mRescanBtn);

  mSignOutBtn = new T3kButton(
      mSignOutBtnRect, "SIGN OUT",
      [this]() { this->doSignOut(); },
      T3kButton::Variant::Secondary);
  g->AttachControl(mSignOutBtn);

  mCloseBtn = new T3kButton(
      mCloseBtnRect, "CLOSE",
      [this]() { if (mOnClose) mOnClose(); },
      T3kButton::Variant::Primary);
  g->AttachControl(mCloseBtn);

  // Inherit the parent control's hidden state on first attach.
  const bool startHidden = IsHidden();
  mChangeRootBtn->Hide(startHidden);
  mRescanBtn    ->Hide(startHidden);
  mSignOutBtn   ->Hide(startHidden);
  mCloseBtn     ->Hide(startHidden);
}

void T3kSettingsModal::OnMouseDown(float x, float y, const IMouseMod& /*mod*/)
{
  // Click outside the card → close the modal. Inside-the-card clicks
  // fall through to the child T3kButtons (they're flat-attached to
  // IGraphics so they hit-test independently of this control).
  if (!mCardRect.Contains(x, y)) {
    if (mOnClose) mOnClose();
  }
}

void T3kSettingsModal::Hide(bool hide)
{
  IControl::Hide(hide);
  if (mChangeRootBtn) mChangeRootBtn->Hide(hide);
  if (mRescanBtn)     mRescanBtn    ->Hide(hide);
  if (mSignOutBtn)    mSignOutBtn   ->Hide(hide);
  if (mCloseBtn)      mCloseBtn     ->Hide(hide);
  if (!hide) refresh();
}

void T3kSettingsModal::refresh()
{
  mRootPathLabel = ::t3k::settings::instance().tone3000_root;
  if (mRootPathLabel.empty()) {
    mRootPathLabel = "(not set — first-run modal will prompt on next launch)";
  }
  if (::t3k::cloud::sync::isConfigured()) {
    mWorkerUrlLabel = ::t3k::cloud::sync::kLibrarySyncUrl;
  } else {
    mWorkerUrlLabel = "(not configured — library sync disabled)";
  }
  mSignedIn = (::t3k::cloud::Session::instance().state() ==
               ::t3k::cloud::Session::State::SignedIn);
  if (mSignedIn) {
    if (auto u = ::t3k::cloud::Session::instance().currentUser();
        u.has_value() && !u->username.empty()) {
      mSignedInLabel = "Signed in as " + u->username;
    } else {
      mSignedInLabel = "Signed in";
    }
  } else {
    mSignedInLabel = "Signed out";
  }
  if (mSignOutBtn) mSignOutBtn->SetDisabled(!mSignedIn);
  SetDirty(false);
}

void T3kSettingsModal::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Full-window dim layer.
  g.FillRect(kBackdrop, mRECT);

  // Card surface.
  g.FillRoundRect(th::kBgSurface, mCardRect, th::kRadiusLg);
  g.DrawRoundRect(th::kBorder,    mCardRect, th::kRadiusLg, nullptr, 1.f);

  // Title.
  const IRECT titleR(mCardRect.L, mCardRect.T + th::kS5,
                     mCardRect.R, mCardRect.T + th::kS5 + th::kTypeH1 + 8.f);
  g.DrawText(IText(th::kTypeH1, th::kText, th::kFontDisplay,
                   EAlign::Center, EVAlign::Middle),
             "Settings", titleR);

  const float rowL = mCardRect.L + th::kS5;
  const float rowR = mCardRect.R - th::kS5;
  float rowTop = titleR.B + th::kS3;

  // Row 1 — TONE3000 root folder.
  {
    const IRECT labelR(rowL, rowTop, rowR, rowTop + 16.f);
    g.DrawText(IText(th::kTypeSmall, th::kTextDim, th::kFontBodyMed,
                     EAlign::Near, EVAlign::Top),
               "TONE3000 ROOT FOLDER", labelR);
    const IRECT valueR(rowL, labelR.B + 4.f,
                       mChangeRootBtnRect.L - th::kS3,
                       labelR.B + 4.f + 18.f);
    g.DrawText(IText(th::kTypeBody, th::kText, th::kFontBody,
                     EAlign::Near, EVAlign::Top),
               mRootPathLabel.c_str(), valueR);
    rowTop += kRowH;
  }

  // Row 2 — Library hint (next to REFRESH).
  {
    const IRECT labelR(mRescanBtnRect.R + th::kS3,
                       rowTop + (kRowH - 32.f) * 0.5f,
                       rowR,
                       rowTop + (kRowH - 32.f) * 0.5f + 16.f);
    g.DrawText(IText(th::kTypeSmall, th::kTextDim, th::kFontBodyMed,
                     EAlign::Near, EVAlign::Top),
               "LOCAL LIBRARY", labelR);
    const IRECT hintR(labelR.L, labelR.B + 2.f, labelR.R, labelR.B + 2.f + 14.f);
    g.DrawText(IText(th::kTypeSmall, th::kTextMuted, th::kFontBody,
                     EAlign::Near, EVAlign::Top),
               "Re-walk the TONE3000 folder for new .nam files.", hintR);
    rowTop += kRowH;
  }

  // Row 3 — Account status (next to SIGN OUT).
  {
    const IRECT labelR(mSignOutBtnRect.R + th::kS3,
                       rowTop + (kRowH - 32.f) * 0.5f,
                       rowR,
                       rowTop + (kRowH - 32.f) * 0.5f + 16.f);
    g.DrawText(IText(th::kTypeSmall, th::kTextDim, th::kFontBodyMed,
                     EAlign::Near, EVAlign::Top),
               "TONE3000 ACCOUNT", labelR);
    const IRECT statusR(labelR.L, labelR.B + 2.f, labelR.R, labelR.B + 2.f + 14.f);
    g.DrawText(IText(th::kTypeSmall, th::kTextMuted, th::kFontBody,
                     EAlign::Near, EVAlign::Top),
               mSignedInLabel.c_str(), statusR);
    rowTop += kRowH;
  }

  // Row 4 — Worker URL (read-only).
  {
    const IRECT labelR(rowL, rowTop, rowR, rowTop + 16.f);
    g.DrawText(IText(th::kTypeSmall, th::kTextDim, th::kFontBodyMed,
                     EAlign::Near, EVAlign::Top),
               "LIBRARY SYNC WORKER", labelR);
    const IRECT valueR(rowL, labelR.B + 4.f, rowR, labelR.B + 4.f + 18.f);
    g.DrawText(IText(th::kTypeSmall, th::kTextMuted, th::kFontBody,
                     EAlign::Near, EVAlign::Top),
               mWorkerUrlLabel.c_str(), valueR);
  }

  // About block — anchored above the CLOSE button. Three stacked lines
  // (iPlug2's NanoVG DrawText doesn't honor '\n' as a line break).
  const float aboutTop = mCloseBtnRect.T - th::kS5 - 56.f;
  const float lineH = 16.f;
  const IRECT a1(rowL, aboutTop,                 rowR, aboutTop + lineH);
  const IRECT a2(rowL, aboutTop + lineH,         rowR, aboutTop + 2.f * lineH);
  const IRECT a3(rowL, aboutTop + 2.f * lineH,   rowR, aboutTop + 3.f * lineH);
  char vline[160], cline[200], dline[120];
  std::snprintf(vline, sizeof(vline), "%s %s", PLUG_NAME, PLUG_VERSION_STR);
  std::snprintf(cline, sizeof(cline), "%s", PLUG_COPYRIGHT_STR);
  std::snprintf(dline, sizeof(dline), "Built %s", __DATE__);
  g.DrawText(IText(th::kTypeBody,  th::kText,      th::kFontBodyMed,
                   EAlign::Center, EVAlign::Middle), vline, a1);
  g.DrawText(IText(th::kTypeSmall, th::kTextMuted, th::kFontBody,
                   EAlign::Center, EVAlign::Middle), cline, a2);
  g.DrawText(IText(th::kTypeSmall, th::kTextDim,   th::kFontBody,
                   EAlign::Center, EVAlign::Middle), dline, a3);
}

void T3kSettingsModal::pickToneRoot()
{
  IGraphics* g = GetUI();
  if (!g) return;
  WDL_String dir;
  if (!::t3k::settings::instance().tone3000_root.empty()) {
    dir.Set(::t3k::settings::instance().tone3000_root.c_str());
  }
  g->PromptForDirectory(dir,
      [this](const WDL_String& /*fileName*/, const WDL_String& path) {
        if (path.GetLength() <= 0) return;
        ::t3k::settings::instance().tone3000_root = std::string(path.Get());
        ::t3k::settings::save();
        ::t3k::library::LibraryScanner::instance().rescan();
        this->refresh();
      });
}

void T3kSettingsModal::doRescan()
{
  ::t3k::library::LibraryScanner::instance().rescan();
  refresh();
}

void T3kSettingsModal::doSignOut()
{
  ::t3k::cloud::Session::instance().signOut();
  refresh();
}

}  // namespace t3k::ui
