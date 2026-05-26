// T3kSignInPill implementation. See T3kSignInPill.h.

#include "T3kSignInPill.h"

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

T3kSignInPill::T3kSignInPill(const IRECT& bounds,
                             std::function<void()> onClick)
: IControl(bounds)
, mOnClick(std::move(onClick))
{
}

void T3kSignInPill::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // 2026-05-26 polish-pass — switched from kAccent fill (yellow) + kText
  // (white) to white fill + black text. The accent went to #FFFF00 in
  // the v6 pass and white text on top of it was unreadable. Mirrors the
  // T3kButton::Variant::Invert treatment already used for PICK /
  // DOWNLOAD CTAs in the Library and Cloud tabs.
  const IColor fillWhite(255, 255, 255, 255);
  const IColor fillHover(255, 230, 230, 230);
  const IColor fill = mMouseIsOver ? fillHover : fillWhite;
  g.FillRoundRect(fill, mRECT, th::pillRadius(mRECT.H()));

  // Black "Sign in" label centered.
  const IText label(th::kTypeSmall,
                    IColor(255, 0, 0, 0),
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
