// T3kSearchBar implementation. See T3kSearchBar.h.

#include "T3kSearchBar.h"

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// Magnifier glyph: 8px-diameter circle outline plus a 4px diagonal handle.
// Centered vertically in `bounds`, anchored 14px from the left edge.
void DrawMagnifier(IGraphics& g, const IRECT& bounds, const IColor& color)
{
  const float cx = bounds.L + 14.f;
  const float cy = bounds.MH();
  const float r = 4.f;  // 8px diameter
  g.DrawCircle(color, cx, cy, r, /*pBlend*/ nullptr, /*thickness*/ 1.5f);

  // Handle: a 45° diagonal from the lower-right of the glass outward.
  const float handleStart = r * 0.707f;  // sin/cos 45°
  g.DrawLine(color,
             cx + handleStart, cy + handleStart,
             cx + handleStart + 4.f, cy + handleStart + 4.f,
             /*pBlend*/ nullptr,
             /*thickness*/ 1.5f);
}

}  // namespace

T3kSearchBar::T3kSearchBar(const IRECT& bounds,
                           std::function<void(const std::string&)> onChanged,
                           const char* placeholder)
: IControl(bounds)
, mPlaceholder(placeholder ? placeholder : "")
, mOnChanged(std::move(onChanged))
{
}

void T3kSearchBar::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Pill background + outline. NB: pass the height-clamped pill radius —
  // kRadiusPill = 999 would otherwise blow up iPlug2's PathRoundRect (see
  // theme.h for the full diagnosis of the diagonal-lines bug).
  const float pr = th::pillRadius(mRECT.H());
  g.FillRoundRect(th::kBgSurface, mRECT, pr);
  g.DrawRoundRect(th::kBorder, mRECT, pr, /*pBlend*/ nullptr, /*thickness*/ 1.f);

  // Magnifier glyph at left.
  DrawMagnifier(g, mRECT, th::kTextMuted);

  // Text region begins 32px from the left of the bar.
  const IRECT textArea(mRECT.L + 32.f, mRECT.T, mRECT.R - th::kS3, mRECT.B);

  const bool empty = mValue.empty();
  const IColor textCol = empty ? th::kTextMuted : th::kText;
  const char* str = empty ? mPlaceholder.c_str() : mValue.c_str();

  const IText label(th::kTypeBody,
                    textCol,
                    th::kFontBody,
                    EAlign::Near,
                    EVAlign::Middle);
  g.DrawText(label, str, textArea);
}

void T3kSearchBar::OnMouseDown(float /*x*/, float /*y*/, const IMouseMod& /*mod*/)
{
  namespace th = ::t3k::theme;

  // Open iPlug2's platform text-entry overlay positioned over the editable area.
  const IRECT entryRect(mRECT.L + 32.f, mRECT.T, mRECT.R - th::kS3, mRECT.B);
  // IText defaults to white text-entry background — that flashes the
  // search bar white the moment the user clicks it. Override with the
  // surface tokens so the entry stays dark and blends into the pill.
  const IText entryText = IText(th::kTypeBody,
                                th::kText,
                                th::kFontBody,
                                EAlign::Near,
                                EVAlign::Middle)
                              .WithTEColors(th::kBgSurface, th::kText);

  // FIXME(t3k): iPlug2's CreateTextEntry commits on Enter / focus-loss only —
  // per-keystroke onChanged is not supported through this path. The current
  // contract therefore fires onChanged once on commit. Per-keystroke filtering
  // would require attaching ITextEntryControl directly with a custom
  // OnStateChanged hook; deferred until a consumer asks for it.
  if (auto* ui = GetUI())
    ui->CreateTextEntry(*this, entryText, entryRect, mValue.c_str());
}

void T3kSearchBar::OnTextEntryCompletion(const char* str, int /*valIdx*/)
{
  mValue = (str ? str : "");
  if (mOnChanged) mOnChanged(mValue);
  SetDirty(false);
}

}  // namespace t3k::ui
