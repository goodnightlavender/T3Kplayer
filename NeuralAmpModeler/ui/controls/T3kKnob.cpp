// T3kKnob implementation. See T3kKnob.h.

#include "T3kKnob.h"

#include <algorithm>
#include <cmath>

#include "IGraphics.h"
#include "IGraphicsStructs.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// Sweep range: -135° → +135° (clockwise). 0° is up in iPlug2's PathArc.
constexpr float kAngleMin = -135.f;
constexpr float kAngleMax =  135.f;
constexpr float kArcThickness = 3.f;
constexpr float kIndicatorThickness = 2.f;

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// Convert iPlug2's "0° is up, clockwise" convention into (dx, dy) on a unit
// circle. Result has dy negative when the angle is "up" (because screen Y is
// inverted), matching how PathArc draws.
void AngleToUnitVec(float angleDeg, float& outX, float& outY)
{
  const float r = angleDeg * 3.14159265358979323846f / 180.f;
  outX = std::sin(r);
  outY = -std::cos(r);
}

}  // namespace

T3kKnob::T3kKnob(const IRECT& bounds, int paramIdx, const char* label)
: IKnobControlBase(bounds, paramIdx)
, mLabel(label)
{
}

// 2026-05-26 (Phase G1) — mouse handlers: forward to IKnobControlBase so it
// drives the standard drag-to-change param math, then flag this knob as the
// active one (drives the yellow wash) and fan the touch event out to the
// parent's readout sink.
void T3kKnob::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  IKnobControlBase::OnMouseDown(x, y, mod);
  setActive(true);
  if (mOnTouchOrChange) mOnTouchOrChange(this);
}

void T3kKnob::OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod)
{
  IKnobControlBase::OnMouseDrag(x, y, dX, dY, mod);
  if (mOnTouchOrChange) mOnTouchOrChange(this);
}

void T3kKnob::OnMouseUp(float x, float y, const IMouseMod& mod)
{
  IKnobControlBase::OnMouseUp(x, y, mod);
  setActive(false);
}

void T3kKnob::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // 2026-05-26 — Phase C7: if active, paint a faint yellow wash behind the
  // entire knob cell (including the label strip) so the focused readout
  // visually traces back to its source knob.
  if (mActive)
  {
    const IColor wash(48, 255, 255, 0);
    g.FillRoundRect(wash, mRECT, 4.f);
  }

  // Reserve a strip at the bottom for the label.
  const float labelH = th::kTypeLabel + th::kS1;
  const IRECT knobR = mRECT.GetReducedFromBottom(labelH);

  // 2026-05-26 polish-pass — cap the ring to a 64 px diameter (r = 32),
  // up from the original 44 px (r = 22) cap which read too small at the
  // plug-in's real canvas size. Cells smaller than 64 px continue to
  // scale the ring down to fit.
  const float diameter = std::min(knobR.W(), knobR.H());
  const float r = std::min(diameter * 0.5f, 32.f);
  const float cx = knobR.MW();
  const float cy = knobR.MH();

  // ── Recessed base: flat surface fill. A radial gradient here used to
  // produce faint diagonal banding bleed-through when multiple knobs sat
  // side-by-side — the user reported it as "random lines all over the UI".
  // Solid fill matches the rest of the v6 surface palette and reads as a
  // recessed dial via the 1px outline below.
  g.FillCircle(th::kBgSurface, cx, cy, r);

  // 1px outline around the base.
  g.DrawCircle(th::kBorder, cx, cy, r, /*pBlend*/ nullptr, /*thickness*/ 1.f);

  // ── Arc track ─────────────────────────────────────────────────────────
  // The arc is drawn inside the base — pull it in slightly so it isn't
  // clipped by the outline.
  const float arcR = r - kArcThickness * 1.5f;

  // Background arc (full 270° sweep).
  g.DrawArc(th::kBgElevated, cx, cy, arcR, kAngleMin, kAngleMax,
            /*pBlend*/ nullptr, kArcThickness);

  // Filled arc: proportional to current normalized value.
  const float v = float(std::clamp(GetValue(), 0.0, 1.0));
  const float fillEnd = Lerp(kAngleMin, kAngleMax, v);
  if (v > 0.f)
  {
    g.DrawArc(th::kAccent, cx, cy, arcR, kAngleMin, fillEnd,
              /*pBlend*/ nullptr, kArcThickness);
  }

  // ── Indicator line ────────────────────────────────────────────────────
  float dx, dy;
  AngleToUnitVec(fillEnd, dx, dy);
  // Run the line from a small inset to the inner edge of the arc track.
  const float innerR = r * 0.25f;
  const float outerR = arcR - kArcThickness;
  g.DrawLine(th::kAccent,
             cx + dx * innerR, cy + dy * innerR,
             cx + dx * outerR, cy + dy * outerR,
             /*pBlend*/ nullptr,
             kIndicatorThickness);

  // ── Label ─────────────────────────────────────────────────────────────
  const IRECT labelRect(mRECT.L, knobR.B, mRECT.R, mRECT.B);
  const IText label(th::kTypeLabel,
                    th::kTextMuted,
                    th::kFontBodyMed,
                    EAlign::Center,
                    EVAlign::Middle);
  g.DrawText(label, mLabel, labelRect);
}

}  // namespace t3k::ui
