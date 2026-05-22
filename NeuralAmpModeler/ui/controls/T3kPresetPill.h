// T3kPresetPill — header-right preset selector pill (Phase 2b, v6).
//
// Visual (matches .superpowers/brainstorm/.../tone-tab-v6.html):
//   - 24px tall, auto-width content (6px dot + name text + 10px "▾"),
//     max-width 220px with ellipsis on the name.
//   - kBgSurface fill, 1px kBorder stroke, kRadiusPill rounding.
//   - The leading 6px dot is kAccent when clean and kWarning (amber) when
//     the current preset has unsaved changes.
//
// Interaction: clicking anywhere on the pill fires the onToggleOverlay
// callback. The parent (ToneRoot) owns the T3kPresetOverlay and decides
// whether to show or hide it.

#pragma once

#include <functional>
#include <string>

#include "IControl.h"
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::IMouseMod;

class T3kPresetPill : public IControl
{
public:
  T3kPresetPill(const IRECT& bounds,
                std::function<void()> onToggleOverlay);

  void setActivePresetName(std::string name);
  void setDirty(bool dirty);

  bool dirty() const { return mDirty; }
  const std::string& activePresetName() const { return mActiveName; }

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const IMouseMod& mod) override;
  void OnMouseOut() override;

private:
  std::string mActiveName;
  bool mDirty = false;
  std::function<void()> mOnToggleOverlay;
};

}  // namespace t3k::ui
