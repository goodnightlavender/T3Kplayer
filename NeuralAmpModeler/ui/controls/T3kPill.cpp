// T3kPill implementation. See T3kPill.h.

#include "T3kPill.h"

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

T3kPill::T3kPill(const IRECT& bounds,
                 const char* label,
                 Mode mode,
                 std::function<void(bool)> onToggle)
: IControl(bounds)
, mLabel(label)
, mMode(mode)
, mOnToggle(std::move(onToggle))
{
}

void T3kPill::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  const IColor stroke = mOn ? th::kBorderActive : th::kBorder;
  const IColor textCol = mOn ? th::kText : th::kTextMuted;

  g.DrawRoundRect(stroke, mRECT, th::kRadiusPill, /*pBlend*/ nullptr, /*thickness*/ 1.f);

  const IText label(th::kTypeSmall,
                    textCol,
                    th::kFontBody,
                    EAlign::Center,
                    EVAlign::Middle);
  g.DrawText(label, mLabel, mRECT);
}

void T3kPill::OnMouseDown(float /*x*/, float /*y*/, const IMouseMod& /*mod*/)
{
  if (mMode != Mode::Toggle) return;

  mOn = !mOn;
  if (mOnToggle) mOnToggle(mOn);
  SetDirty(false);
}

}  // namespace t3k::ui
