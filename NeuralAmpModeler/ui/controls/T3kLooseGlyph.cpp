// T3kLooseGlyph implementation. See T3kLooseGlyph.h.

#include "T3kLooseGlyph.h"

#include <algorithm>

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// The SVG ships at #cfcfcf strokes; that's our default state. For the
// disabled state we paint a translucent black overlay on top of the SVG
// to fake the dimmer color (iPlug2/NanoVG at this revision doesn't expose
// stroke-color override on a loaded SVG without rewriting the file).
constexpr float kIconPad = 2.f;

}  // namespace

T3kLooseGlyph::T3kLooseGlyph(const IRECT& bounds,
                             const char* svgFilename,
                             std::function<void()> onClick,
                             bool disabled)
: IControl(bounds)
, mSvgFilename(svgFilename ? svgFilename : "")
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

  if (mSvgFilename.empty()) return;

  if (!mSvg.has_value())
    mSvg.emplace(g.LoadSVG(mSvgFilename.c_str()));

  // Center the icon in a square sized to the smaller of the bounds. The
  // header lays this out as ~18×~36 (kLooseW × header padding), so the
  // square keeps the icon a consistent 14×14 visual weight.
  const float side = std::min(mRECT.W(), mRECT.H()) - kIconPad * 2.f;
  const float cx = mRECT.MW();
  const float cy = mRECT.MH();
  const IRECT iconR(cx - side * 0.5f, cy - side * 0.5f,
                    cx + side * 0.5f, cy + side * 0.5f);
  g.DrawSVG(*mSvg, iconR);

  // Disabled = dim by painting a translucent black rect on top. (Same
  // limitation T3kSlot's icon faces — no stroke retint without reloading
  // the SVG with a different fill color string.)
  if (mDisabled) {
    const IColor dimOverlay(160, 0, 0, 0);
    g.FillRect(dimOverlay, mRECT);
  }
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
