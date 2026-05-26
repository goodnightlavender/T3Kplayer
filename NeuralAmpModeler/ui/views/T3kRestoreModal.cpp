// T3kRestoreModal.cpp — see T3kRestoreModal.h.

#include "T3kRestoreModal.h"

#include <cstdio>

#include "IGraphics.h"

#include "../theme.h"
#include "../controls/T3kButton.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

constexpr float kCardW   = 520.f;
constexpr float kCardH   = 260.f;
constexpr float kBtnW    = 140.f;
constexpr float kBtnH    = 32.f;
constexpr float kBtnGap  = 12.f;

// Backdrop alpha — slightly lighter than T3kFirstRunModal's 78% so the
// underlying tab content remains visible behind the card. The restore
// modal is informational; the first-run modal is gating.
const IColor kBackdrop {153,   0,   0,   0};  // ~60% alpha black

}  // namespace

T3kRestoreModal::T3kRestoreModal(const IRECT& bounds,
                                 OnChoice onRestore,
                                 OnChoice onDismiss)
: IControl(bounds)
, mOnRestore(std::move(onRestore))
, mOnDismiss(std::move(onDismiss))
{
  OnResize();
}

void T3kRestoreModal::setMissingCount(int n)
{
  mMissingCount = (n < 0 ? 0 : n);
  SetDirty(false);
}

void T3kRestoreModal::OnResize()
{
  const float cx = mRECT.MW();
  const float cy = mRECT.MH();
  mCardRect = IRECT(cx - kCardW * 0.5f, cy - kCardH * 0.5f,
                    cx + kCardW * 0.5f, cy + kCardH * 0.5f);

  // Buttons centered along the bottom of the card.
  const float btnRowW = 2.f * kBtnW + kBtnGap;
  const float btnRowL = mCardRect.MW() - btnRowW * 0.5f;
  const float btnY    = mCardRect.B - t3k::theme::kS5 - kBtnH;
  mPrimaryBtnRect   = IRECT(btnRowL,                  btnY,
                            btnRowL + kBtnW,          btnY + kBtnH);
  mSecondaryBtnRect = IRECT(btnRowL + kBtnW + kBtnGap, btnY,
                            btnRowL + kBtnW + kBtnGap + kBtnW, btnY + kBtnH);

  if (mRestoreBtn) mRestoreBtn->SetTargetAndDrawRECTs(mPrimaryBtnRect);
  if (mDismissBtn) mDismissBtn->SetTargetAndDrawRECTs(mSecondaryBtnRect);
}

void T3kRestoreModal::OnAttached()
{
  IGraphics* g = GetUI();
  if (!g) return;

  // 2026-05-26 polish-pass — same Invert flip as the other CTAs.
  mRestoreBtn = new T3kButton(
      mPrimaryBtnRect,
      "RESTORE ALL",
      [this]() { if (mOnRestore) mOnRestore(); },
      T3kButton::Variant::Invert);
  g->AttachControl(mRestoreBtn);

  mDismissBtn = new T3kButton(
      mSecondaryBtnRect,
      "NOT NOW",
      [this]() { if (mOnDismiss) mOnDismiss(); },
      T3kButton::Variant::Secondary);
  g->AttachControl(mDismissBtn);

  // Match initial hidden state of the parent. ToneRoot attaches us
  // hidden and only flips us on when LibrarySync reports a non-zero
  // pull with local-missing rows.
  const bool startHidden = IsHidden();
  mRestoreBtn->Hide(startHidden);
  mDismissBtn->Hide(startHidden);
}

void T3kRestoreModal::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Full-window dim layer.
  g.FillRect(kBackdrop, mRECT);

  // Card surface.
  g.FillRoundRect(th::kBgSurface, mCardRect, th::kRadiusLg);
  g.DrawRoundRect(th::kBorder,    mCardRect, th::kRadiusLg, nullptr, 1.f);

  // Title (Anton 24px).
  const IRECT titleR(mCardRect.L, mCardRect.T + th::kS5,
                     mCardRect.R, mCardRect.T + th::kS5 + th::kTypeH1 + 8.f);
  g.DrawText(IText(th::kTypeH1, th::kText, th::kFontDisplay,
                   EAlign::Center, EVAlign::Middle),
             "Restore your library?",
             titleR);

  // Two-line body. iPlug2's NanoVG DrawText doesn't honor '\n' as a
  // line break (same workaround as T3kFirstRunModal).
  const float kLineH = th::kTypeBody + 8.f;

  char line1[160];
  if (mMissingCount == 1) {
    std::snprintf(line1, sizeof(line1),
                  "1 tone from your TONE3000 library isn't on this device.");
  } else {
    std::snprintf(line1, sizeof(line1),
                  "%d tones from your TONE3000 library aren't on this device.",
                  mMissingCount);
  }

  const IRECT line1R(mCardRect.L + th::kS5,
                     titleR.B + th::kS3,
                     mCardRect.R - th::kS5,
                     titleR.B + th::kS3 + kLineH);
  const IRECT line2R(line1R.L, line1R.B, line1R.R, line1R.B + kLineH);
  const IText body(th::kTypeBody, th::kTextMuted, th::kFontBody,
                   EAlign::Center, EVAlign::Middle);
  g.DrawText(body, line1, line1R);
  g.DrawText(body,
             "Redownload them now? You can also do this anytime per-tone.",
             line2R);
}

void T3kRestoreModal::Hide(bool hide)
{
  IControl::Hide(hide);
  // Children are attached flat to IGraphics (see OnAttached), not
  // parented through this control's draw tree — so Hide() doesn't
  // cascade automatically. Forward it ourselves.
  if (mRestoreBtn) mRestoreBtn->Hide(hide);
  if (mDismissBtn) mDismissBtn->Hide(hide);
}

}  // namespace t3k::ui
