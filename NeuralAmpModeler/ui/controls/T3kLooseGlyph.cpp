// T3kLooseGlyph implementation. See T3kLooseGlyph.h.

#include "T3kLooseGlyph.h"

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

T3kLooseGlyph::T3kLooseGlyph(const IRECT& bounds,
                             const char* glyph,
                             std::function<void()> onClick,
                             bool disabled)
: IControl(bounds)
, mGlyph(glyph ? glyph : "")
, mOnClick(std::move(onClick))
, mDisabled(disabled)
{
}

void T3kLooseGlyph::setDisabled(bool d)
{
  if (d == mDisabled) return;
  mDisabled = d;
  SetDirty(false);
}

void T3kLooseGlyph::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Default → muted. Hover (and not disabled) → bright text. Disabled →
  // border color (very dim) so the glyph reads as inactive.
  IColor col;
  if (mDisabled)
    col = th::kBorder;
  else if (mMouseIsOver)
    col = th::kText;
  else
    col = th::kTextMuted;

  // Slightly larger than body text — these glyphs are pure decoration and
  // need to read as icons rather than letters.
  const IText label(th::kTypeH2,
                    col,
                    th::kFontBody,
                    EAlign::Center,
                    EVAlign::Middle);
  g.DrawText(label, mGlyph.c_str(), mRECT);
}

void T3kLooseGlyph::OnMouseDown(float /*x*/, float /*y*/, const IMouseMod& /*mod*/)
{
  if (mDisabled) return;
  if (mOnClick) mOnClick();
  SetDirty(false);
}

void T3kLooseGlyph::OnMouseOver(float x, float y, const IMouseMod& mod)
{
  IControl::OnMouseOver(x, y, mod);
  SetDirty(false);
}

void T3kLooseGlyph::OnMouseOut()
{
  IControl::OnMouseOut();
  SetDirty(false);
}

}  // namespace t3k::ui
