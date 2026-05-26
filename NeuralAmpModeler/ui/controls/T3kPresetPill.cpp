// T3kPresetPill implementation. See T3kPresetPill.h.

#include "T3kPresetPill.h"

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// Pill internal layout constants (see v6 mockup .plg-preset-name CSS).
constexpr float kDotR         = 3.f;   // 6px diameter
constexpr float kPadLeft      = 12.f;  // dot center inset from left edge
constexpr float kPadRight     = 8.f;
constexpr float kChevSize     = 10.f;
constexpr float kChevGap      = 4.f;   // gap between name and chevron
constexpr float kDotToTextGap = 7.f;

// Truncate a name to fit `maxW` pixels at the given text size by appending
// a U+2026 ellipsis. Cheap and deterministic — measures via
// IGraphics::MeasureText.
std::string ClampName(IGraphics& g, const IText& text, const std::string& name, float maxW)
{
  IRECT probe(0.f, 0.f, 10000.f, 1000.f);
  g.MeasureText(text, name.c_str(), probe);
  if (probe.W() <= maxW) return name;

  std::string out = name;
  while (!out.empty()) {
    out.pop_back();
    const std::string candidate = out + "\xE2\x80\xA6";
    IRECT m(0.f, 0.f, 10000.f, 1000.f);
    g.MeasureText(text, candidate.c_str(), m);
    if (m.W() <= maxW) return candidate;
  }
  return std::string("\xE2\x80\xA6");
}

}  // namespace

T3kPresetPill::T3kPresetPill(const IRECT& bounds,
                             std::function<void()> onToggleOverlay)
: IControl(bounds)
, mActiveName("Default")
, mOnToggleOverlay(std::move(onToggleOverlay))
{
}

void T3kPresetPill::setActivePresetName(std::string name)
{
  if (name == mActiveName) return;
  mActiveName = std::move(name);
  SetDirty(false);
}

void T3kPresetPill::setDirty(bool dirty)
{
  if (dirty == mDirty) return;
  mDirty = dirty;
  SetDirty(false);
}

void T3kPresetPill::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Pill background + outline. Hover slightly brightens the border so the
  // affordance reads on mouse-over (matches the .plg-preset-name:hover CSS).
  const IColor stroke = mMouseIsOver ? th::kBorderActive : th::kBorder;
  // Clamp pill radius to half-height — see theme.h diagnosis.
  const float pr = th::pillRadius(mRECT.H());
  g.FillRoundRect(th::kBgSurface, mRECT, pr);
  g.DrawRoundRect(stroke, mRECT, pr, nullptr, 1.f);

  // Dot — clean (accent blue) vs dirty (warning amber).
  const float dotCx = mRECT.L + kPadLeft;
  const float dotCy = mRECT.MH();
  const IColor dotCol = mDirty ? th::kWarning : th::kAccent;
  g.FillCircle(dotCol, dotCx, dotCy, kDotR);

  // Compute text + chevron region. Chevron lives at the right edge; name
  // text fills what's left between the dot and the chevron.
  const float chevR = mRECT.R - kPadRight;
  const IRECT chevRect(chevR - kChevSize, mRECT.T, chevR, mRECT.B);
  const float textL = dotCx + kDotR + kDotToTextGap;
  const float textR = chevRect.L - kChevGap;
  const IRECT textRect(textL, mRECT.T, textR, mRECT.B);

  const IText nameText(th::kTypeSmall,
                       th::kText,
                       th::kFontBodySemi,
                       EAlign::Near,
                       EVAlign::Middle);
  const std::string clamped = ClampName(g, nameText, mActiveName, textRect.W());
  g.DrawText(nameText, clamped.c_str(), textRect);

  // Inter doesn't ship the U+25BE "▾" glyph in the subset we bundle, so
  // text-rendering it produces a tofu box. Draw the chevron as a small
  // downward-pointing FillTriangle instead — same visual, no font dep.
  {
    const float cx = chevRect.MW();
    const float cy = chevRect.MH();
    const float halfW = 4.f;  // 8px wide
    const float halfH = 2.5f; // 5px tall
    g.FillTriangle(th::kTextMuted,
                   cx - halfW, cy - halfH,
                   cx + halfW, cy - halfH,
                   cx,         cy + halfH,
                   nullptr);
  }
}

void T3kPresetPill::OnMouseDown(float /*x*/, float /*y*/, const IMouseMod& /*mod*/)
{
  if (mOnToggleOverlay) mOnToggleOverlay();
  SetDirty(false);
}

void T3kPresetPill::OnMouseOver(float x, float y, const IMouseMod& mod)
{
  IControl::OnMouseOver(x, y, mod);
  SetDirty(false);
}

void T3kPresetPill::OnMouseOut()
{
  IControl::OnMouseOut();
  SetDirty(false);
}

}  // namespace t3k::ui
