// T3kButton implementation. See T3kButton.h for variant descriptions.

#include "T3kButton.h"

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// Lighten a color toward white by a fraction in [0,1].
// 0   → unchanged, 1 → pure white. Alpha is preserved.
IColor LightenTowardWhite(const IColor& c, float frac)
{
  const float k = (frac < 0.f ? 0.f : (frac > 1.f ? 1.f : frac));
  auto mix = [k](int ch) {
    const float v = float(ch) + (255.f - float(ch)) * k;
    const int iv = int(v + 0.5f);
    return iv < 0 ? 0 : (iv > 255 ? 255 : iv);
  };
  return IColor(c.A, mix(c.R), mix(c.G), mix(c.B));
}

}  // namespace

T3kButton::T3kButton(const IRECT& bounds,
                     const char* label,
                     std::function<void()> onClick,
                     Variant variant)
: IControl(bounds)
, mLabel(label ? label : "")
, mOnClick(std::move(onClick))
, mVariant(variant)
{
}

void T3kButton::setLabel(const char* label)
{
  mLabel = (label ? label : "");
  SetDirty(false);
}

void T3kButton::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  const bool hover = mMouseIsOver;

  // Clamp the pill radius to the button's half-height — kRadiusPill = 999
  // would otherwise blow iPlug2's PathRoundRect into screen-spanning
  // arcs (see theme.h diagnosis).
  const float pr = th::pillRadius(mRECT.H());
  // 2026-05-26 — Invert variant: white fill + black text. Used for the
  // PICK/LOAD and DOWNLOAD CTAs which used to be Primary (yellow fill +
  // white text — unreadable since the accent went to #FFFF00).
  IColor textColor = th::kText;
  if (mVariant == Variant::Primary)
  {
    const IColor fill = hover ? LightenTowardWhite(th::kAccent, 0.10f) : th::kAccent;
    g.FillRoundRect(fill, mRECT, pr);
  }
  else if (mVariant == Variant::Invert)
  {
    // White fill (very slightly dimmed on hover for feedback); text in
    // theme-black so the affordance contrasts cleanly.
    const IColor white(255, 255, 255, 255);
    const IColor hoverWhite(255, 232, 232, 232);
    g.FillRoundRect(hover ? hoverWhite : white, mRECT, pr);
    textColor = IColor(255, 0, 0, 0);
  }
  else  // Secondary
  {
    // Transparent background; outline indicates the affordance.
    const IColor stroke = hover ? th::kBorderActive : th::kBorder;
    g.DrawRoundRect(stroke, mRECT, pr, /*pBlend*/ nullptr, /*thickness*/ 1.f);
  }

  const IText label(th::kTypeBody,
                    textColor,
                    th::kFontBodyMed,
                    EAlign::Center,
                    EVAlign::Middle);
  g.DrawText(label, mLabel.c_str(), mRECT);
}

void T3kButton::OnMouseDown(float /*x*/, float /*y*/, const IMouseMod& /*mod*/)
{
  if (mOnClick) mOnClick();
  SetDirty(false);
}

void T3kButton::OnMouseOver(float x, float y, const IMouseMod& mod)
{
  IControl::OnMouseOver(x, y, mod);  // updates mMouseIsOver
  SetDirty(false);
}

void T3kButton::OnMouseOut()
{
  IControl::OnMouseOut();
  SetDirty(false);
}

}  // namespace t3k::ui
