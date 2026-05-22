// LibraryView.cpp — see LibraryView.h.

#include "LibraryView.h"

#include "../theme.h"
#include "IGraphics.h"

namespace t3k::ui {

using namespace iplug::igraphics;

LibraryView::LibraryView(const IRECT& bounds)
  : IControl(bounds)
{}

void LibraryView::Draw(IGraphics& g)
{
  namespace th = t3k::theme;
  g.DrawText(IText(th::kTypeBody, th::kTextMuted,
                   th::kFontBody, EAlign::Center),
             "Library — coming in Phase 3",
             mRECT);
}

}  // namespace t3k::ui
