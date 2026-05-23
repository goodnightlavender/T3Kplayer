// T3kFirstRunModal.cpp — see T3kFirstRunModal.h.

#include "T3kFirstRunModal.h"

#include "IGraphics.h"
#include "wdlstring.h"  // WDL_String

#include "../theme.h"
#include "../controls/T3kButton.h"
#include "../../library/Paths.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

constexpr float kCardW   = 480.f;
constexpr float kCardH   = 320.f;
constexpr float kBtnW    = 200.f;
constexpr float kBtnH    = 36.f;
constexpr float kBtnGap  = 12.f;

// Backdrop alpha for the dim layer. Sits on top of the underlying tab
// body so the user can't accidentally click through to a slot or a tab.
const IColor kBackdrop {200,   0,   0,   0};  // ~78% alpha black

}  // namespace

T3kFirstRunModal::T3kFirstRunModal(const IRECT& bounds, OnPicked onPicked)
: IControl(bounds)
, mOnPicked(std::move(onPicked))
{
  mSuggestedPath = ::t3k::library::Paths::suggestedToneRoot();
  OnResize();
}

void T3kFirstRunModal::OnResize()
{
  const float cx = mRECT.MW();
  const float cy = mRECT.MH();
  mCardRect = IRECT(cx - kCardW * 0.5f, cy - kCardH * 0.5f,
                    cx + kCardW * 0.5f, cy + kCardH * 0.5f);

  // Buttons sit centered along the bottom of the card. Total row width:
  // 2 * kBtnW + kBtnGap. Y: card bottom minus button height minus pad.
  const float btnRowW = 2.f * kBtnW + kBtnGap;
  const float btnRowL = mCardRect.MW() - btnRowW * 0.5f;
  const float btnY    = mCardRect.B - t3k::theme::kS5 - kBtnH;
  mPrimaryBtnRect   = IRECT(btnRowL,                btnY, btnRowL + kBtnW,                  btnY + kBtnH);
  mSecondaryBtnRect = IRECT(btnRowL + kBtnW + kBtnGap, btnY,
                            btnRowL + kBtnW + kBtnGap + kBtnW, btnY + kBtnH);

  if (mUseSuggestedBtn) mUseSuggestedBtn->SetTargetAndDrawRECTs(mPrimaryBtnRect);
  if (mPickCustomBtn)   mPickCustomBtn  ->SetTargetAndDrawRECTs(mSecondaryBtnRect);
}

void T3kFirstRunModal::OnAttached()
{
  IGraphics* g = GetUI();
  if (!g) return;

  mUseSuggestedBtn = new T3kButton(
      mPrimaryBtnRect,
      "USE SUGGESTED FOLDER",
      [this]() { onUseSuggested(); },
      T3kButton::Variant::Primary);
  g->AttachControl(mUseSuggestedBtn);

  mPickCustomBtn = new T3kButton(
      mSecondaryBtnRect,
      "PICK CUSTOM\xE2\x80\xA6",  // U+2026
      [this]() { onPickCustom(); },
      T3kButton::Variant::Secondary);
  g->AttachControl(mPickCustomBtn);
}

void T3kFirstRunModal::Draw(IGraphics& g)
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
             "Welcome to TONE3000 Player",
             titleR);

  // Body copy.
  const IRECT bodyR(mCardRect.L + th::kS5,
                    titleR.B + th::kS3,
                    mCardRect.R - th::kS5,
                    titleR.B + th::kS3 + 60.f);
  g.DrawText(IText(th::kTypeBody, th::kTextMuted, th::kFontBody,
                   EAlign::Center, EVAlign::Top),
             "Pick the folder where you keep your TONE3000 models.\n"
             "We'll scan it now and watch it for new downloads.",
             bodyR);

  // Suggested-path line.
  const IRECT pathR(mCardRect.L + th::kS5,
                    bodyR.B + th::kS3,
                    mCardRect.R - th::kS5,
                    bodyR.B + th::kS3 + th::kTypeSmall + 8.f);
  g.DrawText(IText(th::kTypeSmall, th::kTextDim, th::kFontBody,
                   EAlign::Center, EVAlign::Middle),
             mSuggestedPath.empty()
                 ? "<no Documents folder available>"
                 : mSuggestedPath.c_str(),
             pathR);
}

void T3kFirstRunModal::onUseSuggested()
{
  if (mSuggestedPath.empty() || !mOnPicked) return;
  mOnPicked(mSuggestedPath);
}

void T3kFirstRunModal::onPickCustom()
{
  IGraphics* g = GetUI();
  if (!g) return;

  WDL_String dir;
  if (!mSuggestedPath.empty()) dir.Set(mSuggestedPath.c_str());

  // PromptForDirectory's contract (see IGraphics.h §870): the dialog
  // blocks the main thread; on success `dir` is set AND the completion
  // handler fires. Use the handler — it fires only on a real choice
  // (cancel leaves the handler invocation's path empty), which avoids
  // a "pre-seeded suggestion gets falsely confirmed" bug if the user
  // hits Cancel.
  g->PromptForDirectory(dir,
      [this](const WDL_String& /*fileName*/, const WDL_String& path) {
        if (path.GetLength() > 0 && mOnPicked) {
          mOnPicked(std::string(path.Get()));
        }
      });
}

}  // namespace t3k::ui
