#include "T3kGlobalKnob.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include "IGraphics.h"
#include "../theme.h"

namespace t3k::ui {

using namespace iplug::igraphics;

T3kGlobalKnob::T3kGlobalKnob(const IRECT& bounds, int paramIdx, std::string label)
: IKnobControlBase(bounds, paramIdx)
, mLabel(std::move(label))
{
  for (char& c : mLabel)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

void T3kGlobalKnob::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;
  // 2026-05-26 polish-pass — ring 40 (r=20) → 56 (r=28), label/val
  // proportional. Was sized for the 720 px mockup canvas.
  const float cx = mRECT.MW();
  const float cy = mRECT.T + 32.f;
  const float r  = 28.f;

  g.DrawCircle(IColor(255, 42, 42, 42), cx, cy, r, nullptr, 2.5f);

  const double v = GetValue();
  const float start = -135.f;
  const float end   = start + static_cast<float>(v) * 270.f;
  g.DrawArc(th::kAccent, cx, cy, r, start, end, nullptr, 2.5f);

  const float ang = (start + static_cast<float>(v) * 270.f) * (3.14159265f / 180.f);
  const float ix = cx + std::cos(ang) * (r - 5.f);
  const float iy = cy + std::sin(ang) * (r - 5.f);
  g.DrawLine(th::kAccent, cx, cy, ix, iy, nullptr, 2.5f);

  const IRECT lblR(mRECT.L, cy + r + 5.f, mRECT.R, cy + r + 20.f);
  const IText lblT(12.f, th::kTextMuted, th::kFontBodyBold,
                   EAlign::Center, EVAlign::Middle);
  g.DrawText(lblT, mLabel.c_str(), lblR);

  const IRECT numR(mRECT.L, lblR.B, mRECT.R, mRECT.B);
  const IText numT(13.f, th::kText, th::kFontBody,
                   EAlign::Center, EVAlign::Middle);
  char buf[16];
  if (auto* p = GetParam())
    std::snprintf(buf, sizeof(buf), "%+.1f", p->Value());
  else
    std::snprintf(buf, sizeof(buf), "%.2f", v);
  g.DrawText(numT, buf, numR);
}

}  // namespace t3k::ui
