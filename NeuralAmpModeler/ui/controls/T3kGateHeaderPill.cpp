#include "T3kGateHeaderPill.h"

#include <cstdio>
#include "IGraphics.h"
#include "../theme.h"

namespace t3k::ui {

using namespace iplug::igraphics;

T3kGateHeaderPill::T3kGateHeaderPill(const IRECT& bounds, int paramIdx)
: IKnobControlBase(bounds, paramIdx)
{
}

void T3kGateHeaderPill::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;
  const float r = mRECT.H() * 0.5f;
  g.FillRoundRect(IColor(255, 13, 13, 13), mRECT, r);
  g.DrawRoundRect(IColor(255, 51, 51, 51), mRECT, r, nullptr, 1.f);

  const float cx = mRECT.L + 12.f;
  const float cy = mRECT.MH();
  const float ringR = 5.f;
  g.DrawCircle(IColor(255, 51, 51, 51), cx, cy, ringR, nullptr, 1.5f);
  const double v = GetValue();
  const float start = -135.f;
  const float end   = start + static_cast<float>(v) * 270.f;
  g.DrawArc(th::kAccent, cx, cy, ringR, start, end, nullptr, 1.5f);

  const IRECT lblR(cx + ringR + 4.f, mRECT.T, cx + ringR + 4.f + 30.f, mRECT.B);
  const IText lblT(9.f, th::kTextMuted, th::kFontBodyBold,
                   EAlign::Near, EVAlign::Middle);
  g.DrawText(lblT, "GATE", lblR);

  const IRECT numR(lblR.R, mRECT.T, mRECT.R - 8.f, mRECT.B);
  const IText numT(10.f, th::kAccent, th::kFontBody,
                   EAlign::Far, EVAlign::Middle);
  char buf[16];
  if (auto* p = GetParam())
    std::snprintf(buf, sizeof(buf), "%.0f", p->Value());
  else
    std::snprintf(buf, sizeof(buf), "--");
  g.DrawText(numT, buf, numR);
}

}  // namespace t3k::ui
