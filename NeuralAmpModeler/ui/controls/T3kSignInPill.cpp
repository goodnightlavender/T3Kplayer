// T3kSignInPill implementation. See T3kSignInPill.h.

#include "T3kSignInPill.h"

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// Lighten a color toward white. Mirrors T3kButton's helper so the
// hover treatment matches Primary buttons.
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

T3kSignInPill::T3kSignInPill(const IRECT& bounds,
                             std::function<void()> onClick)
: IControl(bounds)
, mOnClick(std::move(onClick))
{
}

void T3kSignInPill::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  const IColor fill = mMouseIsOver
      ? LightenTowardWhite(th::kAccent, 0.10f)
      : th::kAccent;
  g.FillRoundRect(fill, mRECT, th::pillRadius(mRECT.H()));

  // White "Sign in" label centered.
  const IText label(th::kTypeSmall,
                    th::kText,
                    th::kFontBodySemi,
                    EAlign::Center,
                    EVAlign::Middle);
  g.DrawText(label, "Sign in", mRECT);
}

void T3kSignInPill::OnMouseDown(float /*x*/, float /*y*/, const IMouseMod& /*mod*/)
{
  if (mOnClick) mOnClick();
  SetDirty(false);
}

void T3kSignInPill::OnMouseOver(float x, float y, const IMouseMod& mod)
{
  IControl::OnMouseOver(x, y, mod);
  SetDirty(false);
}

void T3kSignInPill::OnMouseOut()
{
  IControl::OnMouseOut();
  SetDirty(false);
}

}  // namespace t3k::ui
