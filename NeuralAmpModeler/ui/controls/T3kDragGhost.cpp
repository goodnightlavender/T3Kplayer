// T3kDragGhost implementation. See T3kDragGhost.h.

#include "T3kDragGhost.h"

#include "T3kSlot.h"
#include "IGraphics.h"

namespace t3k::ui {

T3kDragGhost::T3kDragGhost(const iplug::igraphics::IRECT& bounds)
: IControl(bounds)
{
  // Transparent to all mouse events — the active T3kSlot keeps its
  // captured-mouse status and continues to receive OnMouseDrag /
  // OnMouseUp ticks unimpeded.
  SetIgnoreMouse(true);
}

void T3kDragGhost::setSource(T3kSlot* slot)
{
  mSlot = slot;
  SetDirty(false);
}

void T3kDragGhost::clear()
{
  mSlot = nullptr;
  SetDirty(false);
}

void T3kDragGhost::Draw(iplug::igraphics::IGraphics& g)
{
  if (mSlot) {
    mSlot->drawAtDragOffset(g);
    // While drag smoothing is in flight, keep frames coming. The slot
    // exposes its residual via dragSmoothingActive() so we don't burn
    // frames once the ease has settled.
    if (mSlot->dragSmoothingActive()) SetDirty(false);
  }
}

}  // namespace t3k::ui
