// T3kTabBar — horizontal tab strip with a sliding 2px accent underline.
//
// Visual:
//   - Tab labels laid out left-to-right with kS6 gap between them.
//   - Inactive label color is kTextMuted; active is kText.
//   - A 2px-tall kAccent underline sits beneath the active label and
//     animates (slides + resizes) between tabs over kAnimTabSlide ms.
//
// Interaction: click on a tab to activate it. The onTabChanged callback
// fires with the new index immediately; the underline animates in parallel.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "IControl.h"
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::IMouseMod;

class T3kTabBar : public IControl
{
public:
  T3kTabBar(const IRECT& bounds,
            const std::vector<std::string>& tabs,
            std::function<void(int)> onTabChanged,
            int initialIndex = 0);

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;

  int activeIndex() const { return mActiveIndex; }

private:
  // Compute the rect each label occupies. Cached on first draw or whenever
  // the bar resizes; sized to the text width plus a small horizontal pad.
  void EnsureLabelRects(IGraphics& g);

  // Get the underline rect that sits beneath a given label rect.
  static IRECT UnderlineForLabel(const IRECT& labelRect);

  std::vector<std::string> mTabs;
  std::function<void(int)> mOnTabChanged;
  int mActiveIndex;

  // Cached label hit-rects (computed lazily — depends on font metrics).
  std::vector<IRECT> mLabelRects;
  bool mLabelRectsValid = false;
  // mRECT at the moment mLabelRects was last computed. If mRECT differs
  // from this on the next EnsureLabelRects call (parent resized us), the
  // cache is invalidated and labels are re-measured.
  IRECT mLabelRectsForBar;

  // Animation source/target. We interpolate L and R independently.
  IRECT mUnderlineFrom;
  IRECT mUnderlineTo;
};

}  // namespace t3k::ui
