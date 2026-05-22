// SettingsView.cpp — see SettingsView.h.

#include "SettingsView.h"

#include "../theme.h"
#include "IGraphics.h"

namespace t3k::ui {

using namespace iplug::igraphics;

SettingsView::SettingsView(const IRECT& bounds)
  : IControl(bounds)
{}

void SettingsView::Draw(IGraphics& g)
{
  namespace th = t3k::theme;

  // Overlay surface — same treatment as DownloadsView so the two popups read
  // as a consistent visual class.
  g.FillRoundRect(th::kBgSurface, mRECT, th::kRadiusLg);
  g.DrawRoundRect(th::kBorder, mRECT, th::kRadiusLg,
                  /*pBlend*/ nullptr, /*thickness*/ 1.f);

  g.DrawText(IText(th::kTypeBody, th::kTextMuted,
                   th::kFontBody, EAlign::Center),
             "Settings — coming in Phase 5",
             mRECT);
}

}  // namespace t3k::ui
