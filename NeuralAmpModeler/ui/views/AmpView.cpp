// AmpView.cpp — see AmpView.h.

#include "AmpView.h"

#include "../theme.h"
#include "IGraphics.h"

namespace t3k::ui {

using namespace iplug::igraphics;

AmpView::AmpView(const IRECT& bounds)
  : IControl(bounds)
{
  OnResize();
}

void AmpView::OnResize()
{
  // Reserve a small bottom strip for "model name" text. Rest is a waveform
  // placeholder. Phase 2's AmpView is intentionally thin — Phase 2.x adds
  // waveform/meter rendering.
  const float modelNameH = t3k::theme::kTypeBody + t3k::theme::kS3 * 2.f;
  mModelNameRect = IRECT(mRECT.L, mRECT.B - modelNameH, mRECT.R, mRECT.B);
  mWaveformRect  = IRECT(mRECT.L + t3k::theme::kS5,
                         mRECT.T + t3k::theme::kS5,
                         mRECT.R - t3k::theme::kS5,
                         mRECT.B - modelNameH);
}

void AmpView::Draw(IGraphics& g)
{
  namespace th = t3k::theme;

  // Waveform placeholder — a recessed rounded rect with a 1px border.
  g.FillRoundRect(th::kBgElevated, mWaveformRect, th::kRadiusLg);
  g.DrawRoundRect(th::kBorder, mWaveformRect, th::kRadiusLg,
                  /*pBlend*/ nullptr, /*thickness*/ 1.f);

  // Centered hint text inside the waveform region.
  g.DrawText(IText(th::kTypeBody, th::kTextDim,
                   th::kFontBody, EAlign::Center),
             "Waveform preview — coming post-0.1",
             mWaveformRect);

  // Model-name row at the bottom.
  g.DrawText(IText(th::kTypeBody, th::kTextMuted,
                   th::kFontBodyMed, EAlign::Center),
             "No model loaded",
             mModelNameRect);
}

}  // namespace t3k::ui
