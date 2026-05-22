// T3kAccordion — collapsible section with a 36px header strip + chevron.
//
// Visual:
//   - 36px header: rounded kBgSurface fill, 1px outline (kBorder when
//     closed, kBorderActive when open), label at left, chevron at right.
//   - Chevron is a small "V" formed by two lines, rotated 0° when closed
//     and 90° when open (transition animates over kAnimAccordionChevron ms).
//   - When open, the content rect (everything below the header) is drawn
//     by the caller-provided drawContent functor.
//
// Hit-testing is restricted to the header strip — content interaction is
// the consumer's responsibility.

#pragma once

#include <functional>

#include "IControl.h"
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::IMouseMod;

class T3kAccordion : public IControl
{
public:
  T3kAccordion(const IRECT& bounds,
               const char* label,
               // Consumer reports the height needed when expanded; not used
               // by the accordion itself today but retained for callers that
               // want to compute total layout heights.
               std::function<float()> measureContentHeight,
               std::function<void(const IRECT& contentRect)> drawContent,
               bool initiallyOpen = false);

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;

  // Header-only hit-testing.
  bool IsHit(float x, float y) const override;

  bool isOpen() const { return mOpen; }

private:
  static constexpr float kHeaderH = 36.f;

  IRECT HeaderRect() const;
  IRECT ContentRect() const;

  const char* mLabel;
  std::function<float()> mMeasureContentHeight;
  std::function<void(const IRECT&)> mDrawContent;
  bool mOpen;

  // Chevron animation source/target rotation in degrees.
  float mChevronFromDeg;
  float mChevronToDeg;
};

}  // namespace t3k::ui
