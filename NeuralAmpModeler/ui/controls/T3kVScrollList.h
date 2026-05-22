// T3kVScrollList — virtualized vertical scroll container.
//
// Lays out variable-height items top-to-bottom. Items are NOT IControls;
// the consumer supplies three callbacks (itemCount / itemHeight / drawItem)
// and this control draws only the items currently in view, skipping any
// whose rect is fully outside the bar bounds.
//
// Interaction:
//   - Mouse wheel scrolls by 40px per notch.
//   - Click-drag scrolls the list (touchpad-friendly).
//
// A right-edge 4px scrollbar is drawn whenever total content height exceeds
// the visible height.

#pragma once

#include <functional>

#include "IControl.h"
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::IMouseMod;

class T3kVScrollList : public IControl
{
public:
  T3kVScrollList(const IRECT& bounds,
                 std::function<int()> itemCount,
                 std::function<float(int)> itemHeight,
                 std::function<void(int index, const IRECT& itemRect)> drawItem);

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseUp(float x, float y, const IMouseMod& mod) override;
  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override;
  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override;

  // Force a re-measure on the next Draw. Call when itemCount or itemHeight
  // results have changed.
  void invalidateHeights() { mTotalHeightValid = false; SetDirty(false); }

  // Debug helpers (handy for tests / log output).
  int firstVisibleIndex() const { return mFirstVisible; }
  int lastVisibleIndex() const { return mLastVisible; }

private:
  // Recompute mTotalHeight by summing itemHeight(i) for i in [0, count).
  void EnsureTotalHeight();

  // Clamp mScrollOffset to [0, max(0, totalHeight - visibleHeight)].
  void ClampScrollOffset();

  std::function<int()> mItemCount;
  std::function<float(int)> mItemHeight;
  std::function<void(int, const IRECT&)> mDrawItem;

  float mScrollOffset = 0.f;
  float mTotalHeight = 0.f;
  bool mTotalHeightValid = false;

  // Drag-to-scroll state.
  bool mDragging = false;
  float mDragStartY = 0.f;
  float mDragStartScroll = 0.f;

  // Last computed first/last visible item indices (debug).
  int mFirstVisible = -1;
  int mLastVisible = -1;
};

}  // namespace t3k::ui
