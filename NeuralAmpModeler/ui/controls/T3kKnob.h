// T3kKnob — vector knob with a 270° blue arc indicator and a small label.
//
// Visual:
//   - Recessed circular base (subtle radial gradient + 1px outline).
//   - 270° arc track from -135° to +135° (clockwise), background in
//     kBgElevated, filled portion in kAccent proportional to value.
//   - 2px accent-colored indicator line from center to current angle.
//   - Label drawn beneath the knob in muted text.
//
// Inherits IKnobControlBase so drag-to-change, mouse-wheel adjustment, and
// DAW parameter binding are inherited from iPlug2.

#pragma once

#include <functional>

#include "IControl.h"
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::IKnobControlBase;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;

class T3kKnob : public IKnobControlBase
{
public:
  T3kKnob(const IRECT& bounds, int paramIdx, const char* label);

  void Draw(IGraphics& g) override;

  // 2026-05-26 — active wash: parents call setActive(true) on the currently
  // focused/touched knob so a faint yellow background distinguishes it from
  // its siblings. See Phase C7.
  void setActive(bool a) { if (mActive != a) { mActive = a; SetDirty(false); } }

  // 2026-05-26 (Phase G1) — touch-or-change hook. Fires on mouse-down and on
  // every drag tick so the parent can push the live param value into a
  // sibling display (T3kReadout in the focused panel's title row). The
  // callback receives the knob itself so the parent can read GetParam()
  // without holding a separate handle.
  void setOnTouchOrChange(std::function<void(T3kKnob*)> cb) { mOnTouchOrChange = std::move(cb); }

  void OnMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) override;
  void OnMouseDrag(float x, float y, float dX, float dY, const iplug::igraphics::IMouseMod& mod) override;
  void OnMouseUp(float x, float y, const iplug::igraphics::IMouseMod& mod) override;

private:
  const char* mLabel;
  bool        mActive = false;
  std::function<void(T3kKnob*)> mOnTouchOrChange;
};

}  // namespace t3k::ui
