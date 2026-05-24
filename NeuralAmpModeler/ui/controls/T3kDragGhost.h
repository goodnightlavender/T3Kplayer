// T3kDragGhost — invisible high-z-order overlay that paints whatever
// T3kSlot is currently being dragged.
//
// Why this exists. iPlug2 attaches controls flat; z-order = attach
// order, and there's no public "move control to front" API (the
// internal WDL_PtrList swap is not exposed through IGraphics). The
// dragged T3kSlot has iPlug2's captured-mouse pointer set to itself,
// so destroying and re-attaching the slot to push it to the end of
// the control list would invalidate that capture mid-drag.
//
// The ghost is attached LAST in the strip's z-order, ignores all
// mouse events (so the dragged slot continues to receive drag
// updates), and during a drag paints the active slot at its
// shifted draw rect via T3kSlot::drawAtDragOffset. The slot itself
// skips its own paint while dragging — so we don't double-render.
//
// Lifecycle is owned by ToneView: created in OnAttached after the
// strip's first build, re-attached to the end of the control list
// inside rebuildStrip() so subsequent strip rebuilds don't push
// new tiles in front of it.

#pragma once

#include "IControl.h"

namespace t3k::ui {

class T3kSlot;

class T3kDragGhost : public iplug::igraphics::IControl
{
public:
  explicit T3kDragGhost(const iplug::igraphics::IRECT& bounds);

  // Begin painting `slot`'s drag content. The pointer is non-owning;
  // ToneView clears it via clear() before the slot can be destroyed
  // (drag-end always fires before rebuildStrip in the reorder path).
  void setSource(T3kSlot* slot);
  void clear();

  void Draw(iplug::igraphics::IGraphics& g) override;

private:
  T3kSlot* mSlot = nullptr;  // non-owning
};

}  // namespace t3k::ui
