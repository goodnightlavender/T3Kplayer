// T3kBadge implementation. See T3kBadge.h.

#include "T3kBadge.h"

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

T3kBadge::T3kBadge(const IRECT& bounds, const char* label)
: IControl(bounds)
, mLabel(label)
{
}

void T3kBadge::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  g.DrawRoundRect(th::kBorder, mRECT, th::kRadiusSm, /*pBlend*/ nullptr, /*thickness*/ 1.f);

  const IText label(th::kTypeLabel,
                    th::kTextMuted,
                    th::kFontBody,
                    EAlign::Center,
                    EVAlign::Middle);
  g.DrawText(label, mLabel, mRECT);
}

}  // namespace t3k::ui
