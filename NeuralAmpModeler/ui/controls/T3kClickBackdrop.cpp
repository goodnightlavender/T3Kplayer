// T3kClickBackdrop implementation. See T3kClickBackdrop.h.

#include "T3kClickBackdrop.h"

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

T3kClickBackdrop::T3kClickBackdrop(const IRECT& bounds,
                                   std::function<void()> onClick)
: IControl(bounds)
, mOnClick(std::move(onClick))
{
}

void T3kClickBackdrop::Draw(IGraphics& /*g*/)
{
  // Intentionally empty — backdrop is invisible; it exists for hit-testing only.
}

void T3kClickBackdrop::OnMouseDown(float /*x*/, float /*y*/, const IMouseMod& /*mod*/)
{
  if (mOnClick) mOnClick();
  // Don't SetDirty — the dismiss callback will hide both this backdrop and
  // its companion overlay, which triggers iPlug2's redraw on its own.
}

}  // namespace t3k::ui
