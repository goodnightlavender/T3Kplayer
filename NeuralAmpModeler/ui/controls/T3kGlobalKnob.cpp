#include "T3kGlobalKnob.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include "IGraphics.h"
#include "../theme.h"

namespace t3k::ui {

using namespace iplug::igraphics;

T3kGlobalKnob::T3kGlobalKnob(const IRECT& bounds, int paramIdx, std::string label)
: IKnobControlBase(bounds, paramIdx, EDirection::Vertical, 14.0)
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
  const float knobSide = std::min(64.f, std::min(mRECT.W(), std::max(44.f, mRECT.H() - 48.f)));
  const float cx = mRECT.MW();
  const float cy = mRECT.T + 6.f + knobSide * 0.5f;
  const float r  = knobSide * 0.5f - 4.f;

  g.FillCircle(th::kBgSurface, cx, cy, r);
  g.DrawCircle(th::kBorder, cx, cy, r, nullptr, 1.f);

  const double v = std::clamp(GetValue(), 0.0, 1.0);
  const float start = -135.f;
  const float end   = start + static_cast<float>(v) * 270.f;
  const float arcR = r - 4.5f;
  g.DrawArc(th::kBgElevated, cx, cy, arcR, start, 135.f, nullptr, 3.f);
  if (v > 0.0) g.DrawArc(th::kAccent, cx, cy, arcR, start, end, nullptr, 3.f);

  const float ang = (start + static_cast<float>(v) * 270.f) * (3.14159265f / 180.f);
  const float dx = std::sin(ang);
  const float dy = -std::cos(ang);
  g.DrawLine(th::kAccent,
             cx + dx * (r * 0.25f), cy + dy * (r * 0.25f),
             cx + dx * (arcR - 3.f), cy + dy * (arcR - 3.f),
             nullptr, 2.5f);

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
