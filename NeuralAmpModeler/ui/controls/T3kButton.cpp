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
, mLabel(label)
, mOnClick(std::move(onClick))
, mVariant(variant)
{
}

void T3kButton::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  const bool hover = mMouseIsOver;

  if (mVariant == Variant::Primary)
  {
    const IColor fill = hover ? LightenTowardWhite(th::kAccent, 0.10f) : th::kAccent;
    g.FillRoundRect(fill, mRECT, th::kRadiusPill);
  }
  else  // Secondary
  {
    // Transparent background; outline indicates the affordance.
    const IColor stroke = hover ? th::kBorderActive : th::kBorder;
    g.DrawRoundRect(stroke, mRECT, th::kRadiusPill, /*pBlend*/ nullptr, /*thickness*/ 1.f);
  }

  const IText label(th::kTypeBody,
                    th::kText,
                    th::kFontBodyMed,
                    EAlign::Center,
                    EVAlign::Middle);
  g.DrawText(label, mLabel, mRECT);
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
