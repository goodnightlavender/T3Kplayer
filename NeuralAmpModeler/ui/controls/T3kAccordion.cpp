// T3kAccordion implementation. See T3kAccordion.h.

#include "T3kAccordion.h"

#include <cmath>

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

constexpr float kChevronSize = 6.f;     // half-extent of the chevron glyph
constexpr float kChevronStroke = 1.5f;
constexpr float kDegToRad = 3.14159265358979323846f / 180.f;

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// Rotate (x, y) by `deg` clockwise (matching iPlug2's "0° is up, CW positive"
// convention) around (cx, cy). Returns the transformed point.
void Rotate(float& x, float& y, float cx, float cy, float deg)
{
  const float r = deg * kDegToRad;
  const float s = std::sin(r);
  const float c = std::cos(r);
  const float dx = x - cx;
  const float dy = y - cy;
  x = cx + dx * c - dy * s;
  y = cy + dx * s + dy * c;
}

// Draw a downward-pointing chevron (">"-rotated-90 in default state). At 0°
// rotation it points down (▾); at 90° it points right (▸).
void DrawChevron(IGraphics& g, float cx, float cy, float deg, const IColor& color)
{
  // Base shape (deg=0): a "V" pointing down.
  //   left arm:  (-kChevronSize, -kChevronSize/2) → (0, kChevronSize/2)
  //   right arm: ( kChevronSize, -kChevronSize/2) → (0, kChevronSize/2)
  const float half = kChevronSize * 0.5f;

  float lx0 = cx - kChevronSize, ly0 = cy - half;
  float lx1 = cx,                 ly1 = cy + half;
  float rx0 = cx + kChevronSize, ry0 = cy - half;
  float rx1 = cx,                 ry1 = cy + half;

  Rotate(lx0, ly0, cx, cy, deg);
  Rotate(lx1, ly1, cx, cy, deg);
  Rotate(rx0, ry0, cx, cy, deg);
  Rotate(rx1, ry1, cx, cy, deg);

  g.DrawLine(color, lx0, ly0, lx1, ly1, /*pBlend*/ nullptr, kChevronStroke);
  g.DrawLine(color, rx0, ry0, rx1, ry1, /*pBlend*/ nullptr, kChevronStroke);
}

}  // namespace

T3kAccordion::T3kAccordion(const IRECT& bounds,
                           const char* label,
                           std::function<float()> measureContentHeight,
                           std::function<void(const IRECT&)> drawContent,
                           bool initiallyOpen)
: IControl(bounds)
, mLabel(label)
, mMeasureContentHeight(std::move(measureContentHeight))
, mDrawContent(std::move(drawContent))
, mOpen(initiallyOpen)
, mChevronFromDeg(initiallyOpen ? 90.f : 0.f)
, mChevronToDeg(initiallyOpen ? 90.f : 0.f)
{
}

IRECT T3kAccordion::HeaderRect() const
{
  return IRECT(mRECT.L, mRECT.T, mRECT.R, mRECT.T + kHeaderH);
}

IRECT T3kAccordion::ContentRect() const
{
  return IRECT(mRECT.L, mRECT.T + kHeaderH, mRECT.R, mRECT.B);
}

bool T3kAccordion::IsHit(float x, float y) const
{
  return HeaderRect().Contains(x, y);
}

void T3kAccordion::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  const IRECT header = HeaderRect();

  // Header background + outline.
  g.FillRoundRect(th::kBgSurface, header, th::kRadiusMd);
  const IColor outline = mOpen ? th::kBorderActive : th::kBorder;
  g.DrawRoundRect(outline, header, th::kRadiusMd, /*pBlend*/ nullptr, /*thickness*/ 1.f);

  // Label at the left.
  const IRECT labelRect(header.L + th::kS3, header.T, header.R - th::kS5, header.B);
  const IText label(th::kTypeBody,
                    th::kText,
                    th::kFontBody,
                    EAlign::Near,
                    EVAlign::Middle);
  g.DrawText(label, mLabel, labelRect);

  // Chevron at the right. Compute current rotation (interpolated if
  // animating, else at the target value).
  float deg = mChevronToDeg;
  if (GetAnimationFunction())
  {
    const float t = float(GetAnimationProgress());
    // Ease-out: 1 - (1 - t)^3
    const float eased = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
    deg = Lerp(mChevronFromDeg, mChevronToDeg, eased);
  }

  const float chevCX = header.R - th::kS3 - kChevronSize;
  const float chevCY = header.MH();
  DrawChevron(g, chevCX, chevCY, deg, th::kText);

  // Content area when open.
  if (mOpen && mDrawContent)
  {
    const IRECT content = ContentRect();
    if (content.H() > 0.f) mDrawContent(content);
  }
}

void T3kAccordion::OnMouseDown(float /*x*/, float /*y*/, const IMouseMod& /*mod*/)
{
  // Snapshot the current animated rotation as the new "from" so the next
  // transition picks up where this one left off if it was mid-flight.
  float currentDeg = mChevronToDeg;
  if (GetAnimationFunction())
  {
    const float t = float(GetAnimationProgress());
    const float eased = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
    currentDeg = Lerp(mChevronFromDeg, mChevronToDeg, eased);
  }
  mChevronFromDeg = currentDeg;

  mOpen = !mOpen;
  mChevronToDeg = mOpen ? 90.f : 0.f;

  SetAnimation([](IControl* c) { c->SetDirty(false); },
               ::t3k::theme::kAnimAccordionChevron);

  SetDirty(false);
}

}  // namespace t3k::ui
