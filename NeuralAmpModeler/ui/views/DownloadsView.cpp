// DownloadsView.cpp — see DownloadsView.h.

#include "DownloadsView.h"

#include "../theme.h"
#include "IGraphics.h"

namespace t3k::ui {

using namespace iplug::igraphics;

DownloadsView::DownloadsView(const IRECT& bounds)
  : IControl(bounds)
{}

void DownloadsView::Draw(IGraphics& g)
{
  namespace th = t3k::theme;

  // Overlay surface — opaque background with border, distinct from tab views
  // (which let ToneRoot's body panel show through).
  g.FillRoundRect(th::kBgSurface, mRECT, th::kRadiusLg);
  g.DrawRoundRect(th::kBorder, mRECT, th::kRadiusLg,
                  /*pBlend*/ nullptr, /*thickness*/ 1.f);

  g.DrawText(IText(th::kTypeBody, th::kTextMuted,
                   th::kFontBody, EAlign::Center),
             "No active downloads — coming in Phase 7",
             mRECT);
}

}  // namespace t3k::ui
