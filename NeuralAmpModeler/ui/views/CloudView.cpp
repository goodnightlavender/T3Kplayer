// CloudView.cpp — see CloudView.h.

#include "CloudView.h"

#include "../theme.h"
#include "IGraphics.h"

namespace t3k::ui {

using namespace iplug::igraphics;

CloudView::CloudView(const IRECT& bounds)
  : IControl(bounds)
{}

void CloudView::Draw(IGraphics& g)
{
  namespace th = t3k::theme;
  g.DrawText(IText(th::kTypeBody, th::kTextMuted,
                   th::kFontBody, EAlign::Center),
             "Cloud browser — coming in Phase 6",
             mRECT);
}

}  // namespace t3k::ui
