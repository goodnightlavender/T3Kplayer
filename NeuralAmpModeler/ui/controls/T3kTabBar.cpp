// T3kTabBar implementation. See T3kTabBar.h.

#include "T3kTabBar.h"

#include <algorithm>

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

constexpr float kUnderlineH = 2.f;
constexpr float kLabelHPad  = 2.f;  // tiny horizontal pad around each label

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

IText LabelText(const IColor& color)
{
  namespace th = ::t3k::theme;
  return IText(th::kTypeBody,
               color,
               th::kFontBodySemi,
               EAlign::Center,
               EVAlign::Middle);
}

}  // namespace

T3kTabBar::T3kTabBar(const IRECT& bounds,
                     const std::vector<std::string>& tabs,
                     std::function<void(int)> onTabChanged,
                     int initialIndex)
: IControl(bounds)
, mTabs(tabs)
, mOnTabChanged(std::move(onTabChanged))
, mActiveIndex(initialIndex < 0 ? 0
              : (initialIndex >= int(tabs.size()) ? 0 : initialIndex))
{
}

IRECT T3kTabBar::UnderlineForLabel(const IRECT& labelRect)
{
  // 2px underline sitting directly below the label rect's baseline.
  return IRECT(labelRect.L, labelRect.B, labelRect.R, labelRect.B + kUnderlineH);
}

void T3kTabBar::EnsureLabelRects(IGraphics& g)
{
  if (mLabelRectsValid) return;

  namespace th = ::t3k::theme;

  mLabelRects.clear();
  mLabelRects.reserve(mTabs.size());

  // Vertically center labels in the bar. Reserve kUnderlineH at the bottom
  // so the underline draws within mRECT.
  const IRECT inner = mRECT.GetReducedFromBottom(kUnderlineH);

  float x = mRECT.L;
  for (const auto& tab : mTabs)
  {
    IRECT measured;
    g.MeasureText(LabelText(th::kText), tab.c_str(), measured);
    const float w = measured.W() + kLabelHPad * 2.f;

    mLabelRects.emplace_back(x, inner.T, x + w, inner.B);
    x += w + th::kS6;  // gap between tabs
  }

  mLabelRectsValid = true;

  // Seed the underline animation source/target on the active tab.
  if (!mLabelRects.empty())
  {
    const IRECT& a = mLabelRects[mActiveIndex];
    mUnderlineFrom = mUnderlineTo = UnderlineForLabel(a);
  }
}

void T3kTabBar::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  EnsureLabelRects(g);

  // Draw each label.
  for (size_t i = 0; i < mLabelRects.size(); ++i)
  {
    const bool active = (int(i) == mActiveIndex);
    const IColor c = active ? th::kText : th::kTextMuted;
    g.DrawText(LabelText(c), mTabs[i].c_str(), mLabelRects[i]);
  }

  // Animated underline. If an animation is running, interpolate; otherwise
  // draw at the target.
  IRECT u = mUnderlineTo;
  if (GetAnimationFunction())
  {
    const float t = float(GetAnimationProgress());
    u = IRECT(Lerp(mUnderlineFrom.L, mUnderlineTo.L, t),
              mUnderlineTo.T,
              Lerp(mUnderlineFrom.R, mUnderlineTo.R, t),
              mUnderlineTo.B);
  }
  g.FillRect(th::kAccent, u);
}

void T3kTabBar::OnMouseDown(float x, float y, const IMouseMod& /*mod*/)
{
  if (!mLabelRectsValid) return;

  for (size_t i = 0; i < mLabelRects.size(); ++i)
  {
    if (mLabelRects[i].Contains(x, y))
    {
      if (int(i) == mActiveIndex) return;

      // Kick off the slide animation from the current underline rect to the
      // new tab's underline rect.
      mUnderlineFrom = (GetAnimationFunction()
                         ? IRECT(Lerp(mUnderlineFrom.L, mUnderlineTo.L,
                                      float(GetAnimationProgress())),
                                 mUnderlineTo.T,
                                 Lerp(mUnderlineFrom.R, mUnderlineTo.R,
                                      float(GetAnimationProgress())),
                                 mUnderlineTo.B)
                         : mUnderlineTo);
      mUnderlineTo = UnderlineForLabel(mLabelRects[i]);
      mActiveIndex = int(i);

      SetAnimation([](IControl* c) { c->SetDirty(false); },
                   ::t3k::theme::kAnimTabSlide);

      if (mOnTabChanged) mOnTabChanged(mActiveIndex);
      SetDirty(false);
      return;
    }
  }
}

}  // namespace t3k::ui
