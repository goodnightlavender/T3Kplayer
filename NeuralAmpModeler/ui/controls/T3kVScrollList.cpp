// T3kVScrollList implementation. See T3kVScrollList.h.

#include "T3kVScrollList.h"

#include <algorithm>

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

constexpr float kWheelStepPx   = 40.f;
constexpr float kScrollbarW    = 4.f;

}  // namespace

T3kVScrollList::T3kVScrollList(const IRECT& bounds,
                               std::function<int()> itemCount,
                               std::function<float(int)> itemHeight,
                               std::function<void(int, const IRECT&)> drawItem)
: IControl(bounds)
, mItemCount(std::move(itemCount))
, mItemHeight(std::move(itemHeight))
, mDrawItem(std::move(drawItem))
{
}

void T3kVScrollList::EnsureTotalHeight()
{
  if (mTotalHeightValid) return;

  mTotalHeight = 0.f;
  if (mItemCount && mItemHeight)
  {
    const int n = mItemCount();
    for (int i = 0; i < n; ++i)
    {
      mTotalHeight += mItemHeight(i);
    }
  }
  mTotalHeightValid = true;
}

void T3kVScrollList::ClampScrollOffset()
{
  const float visibleH = mRECT.H();
  const float maxOffset = std::max(0.f, mTotalHeight - visibleH);
  if (mScrollOffset < 0.f)       mScrollOffset = 0.f;
  if (mScrollOffset > maxOffset) mScrollOffset = maxOffset;
}

void T3kVScrollList::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  EnsureTotalHeight();
  ClampScrollOffset();

  if (!mItemCount || !mItemHeight || !mDrawItem)
    return;

  const int n = mItemCount();
  const float visibleH = mRECT.H();

  // Walk items, computing each one's y0/y1 (relative to mRECT.T, accounting
  // for the scroll offset). Skip items entirely outside the visible band.
  mFirstVisible = -1;
  mLastVisible  = -1;

  float yCursor = mRECT.T - mScrollOffset;
  for (int i = 0; i < n; ++i)
  {
    const float h = mItemHeight(i);
    const float y0 = yCursor;
    const float y1 = yCursor + h;
    yCursor = y1;

    // Virtualization: skip items fully above/below the visible band.
    if (y1 < mRECT.T) continue;
    if (y0 > mRECT.B) break;

    if (mFirstVisible == -1) mFirstVisible = i;
    mLastVisible = i;

    // Provide the full item rect (sans the scrollbar gutter on the right);
    // the bar bounds the visible region implicitly.
    const IRECT itemRect(mRECT.L, y0,
                         mRECT.R - kScrollbarW - th::kS1, y1);
    mDrawItem(i, itemRect);
  }

  // Right-edge scrollbar — only if the content overflows.
  if (mTotalHeight > visibleH)
  {
    const IRECT trackRect(mRECT.R - kScrollbarW, mRECT.T,
                          mRECT.R, mRECT.B);
    g.FillRect(th::kBgElevated, trackRect);

    const float thumbH = std::max(16.f, (visibleH / mTotalHeight) * visibleH);
    const float thumbY = mRECT.T + (mScrollOffset / mTotalHeight) * visibleH;
    const IRECT thumbRect(trackRect.L, thumbY,
                          trackRect.R, thumbY + thumbH);
    g.FillRect(th::kTextDim, thumbRect);
  }
}

void T3kVScrollList::OnMouseDown(float /*x*/, float y, const IMouseMod& /*mod*/)
{
  mDragging = true;
  mDragStartY = y;
  mDragStartScroll = mScrollOffset;
}

void T3kVScrollList::OnMouseUp(float /*x*/, float /*y*/, const IMouseMod& /*mod*/)
{
  mDragging = false;
}

void T3kVScrollList::OnMouseDrag(float /*x*/, float y,
                                  float /*dX*/, float /*dY*/,
                                  const IMouseMod& /*mod*/)
{
  if (!mDragging) return;

  mScrollOffset = mDragStartScroll - (y - mDragStartY);
  ClampScrollOffset();
  SetDirty(false);
}

void T3kVScrollList::OnMouseWheel(float /*x*/, float /*y*/,
                                   const IMouseMod& /*mod*/, float d)
{
  EnsureTotalHeight();
  mScrollOffset -= d * kWheelStepPx;
  ClampScrollOffset();
  SetDirty(false);
}

}  // namespace t3k::ui
