// IRView.cpp — see IRView.h.

#include "IRView.h"

#include "../theme.h"
#include "IGraphics.h"

namespace t3k::ui {

using namespace iplug::igraphics;

IRView::IRView(const IRECT& bounds)
  : IControl(bounds)
{}

void IRView::Draw(IGraphics& g)
{
  namespace th = t3k::theme;
  g.DrawText(IText(th::kTypeBody, th::kTextMuted,
                   th::kFontBody, EAlign::Center),
             "Cabinet / IR — coming in Phase 7",
             mRECT);
}

}  // namespace t3k::ui
