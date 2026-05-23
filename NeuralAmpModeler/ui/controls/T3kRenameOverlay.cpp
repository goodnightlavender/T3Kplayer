// T3kRenameOverlay.cpp — see T3kRenameOverlay.h.

#include "T3kRenameOverlay.h"

#include <algorithm>

#include "IGraphics.h"

#include "../theme.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

constexpr float kEntryPad   = 8.f;
constexpr float kLabelH     = 14.f;
constexpr float kEntryH     = 28.f;

}  // namespace

T3kRenameOverlay::T3kRenameOverlay(const IRECT& bounds, OnSave onSave)
: IControl(bounds)
, mOnSave(std::move(onSave))
{
  Hide(true);
}

void T3kRenameOverlay::show(const IRECT& anchorBounds, const std::string& initial)
{
  mValue = initial;

  // Position: anchored below the anchor, left-aligned. Clamp to the
  // graphics rect so the overlay never falls off-screen.
  const float w = std::min(320.f, anchorBounds.W());
  const float h = kEntryPad + kLabelH + 4.f + kEntryH + kEntryPad;
  const float left = anchorBounds.L;
  const float top  = anchorBounds.B + 4.f;

  SetTargetAndDrawRECTs(IRECT(left, top, left + w, top + h));
  Hide(false);
  SetDirty(false);
  openTextEntry();
}

void T3kRenameOverlay::openTextEntry()
{
  IGraphics* g = GetUI();
  if (!g) return;
  namespace th = ::t3k::theme;

  const IRECT entryRect(mRECT.L + kEntryPad,
                        mRECT.T + kEntryPad + kLabelH + 4.f,
                        mRECT.R - kEntryPad,
                        mRECT.B - kEntryPad);
  const IText entryText(th::kTypeBody, th::kText, th::kFontBody,
                        EAlign::Near, EVAlign::Middle);
  g->CreateTextEntry(*this, entryText, entryRect, mValue.c_str());
}

void T3kRenameOverlay::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  g.FillRoundRect(th::kBgSurface, mRECT, th::kRadiusMd);
  g.DrawRoundRect(th::kBorder,    mRECT, th::kRadiusMd, nullptr, 1.f);

  // "RENAME TO" label.
  const IRECT labelR(mRECT.L + kEntryPad,
                     mRECT.T + kEntryPad,
                     mRECT.R - kEntryPad,
                     mRECT.T + kEntryPad + kLabelH);
  g.DrawText(IText(th::kTypeLabel, th::kTextMuted, th::kFontBodyMed,
                   EAlign::Near, EVAlign::Middle),
             "RENAME TO", labelR);

  // Entry rect (drawn as a sunken pill; the platform text entry
  // overlays it when the user is editing).
  const IRECT entryR(mRECT.L + kEntryPad,
                     mRECT.T + kEntryPad + kLabelH + 4.f,
                     mRECT.R - kEntryPad,
                     mRECT.B - kEntryPad);
  g.FillRoundRect(th::kBgBase, entryR, th::kRadiusSm);
  g.DrawRoundRect(th::kBorder, entryR, th::kRadiusSm, nullptr, 1.f);

  // Current value (visible until the user clicks to open the text
  // entry overlay).
  const IRECT textArea(entryR.L + 8.f, entryR.T, entryR.R - 8.f, entryR.B);
  g.DrawText(IText(th::kTypeBody, th::kText, th::kFontBody,
                   EAlign::Near, EVAlign::Middle),
             mValue.empty() ? "\xE2\x80\xA6" : mValue.c_str(),  // U+2026
             textArea);
}

void T3kRenameOverlay::OnMouseDown(float /*x*/, float /*y*/,
                                    const IMouseMod& /*mod*/)
{
  // Clicking anywhere on the overlay re-opens the text entry — useful
  // if the user clicked outside the IGraphics text-entry by accident.
  openTextEntry();
}

void T3kRenameOverlay::OnTextEntryCompletion(const char* str, int /*valIdx*/)
{
  // Esc → str is null on iPlug2; Enter / focus-loss → str is the new
  // value. Empty + clean cancel both Hide.
  if (!str) {
    Hide(true);
    SetDirty(false);
    return;
  }
  mValue = str;
  if (mOnSave) mOnSave(mValue);
  Hide(true);
  SetDirty(false);
}

}  // namespace t3k::ui
