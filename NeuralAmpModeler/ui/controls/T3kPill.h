// T3kPill — outlined pill that is either toggleable or purely informational.
//
// Always rendered as an outline (never filled). When toggled on, the outline
// uses kBorderActive and the label uses kText; when off, the outline is
// kBorder and the label is kTextMuted. Mode::Static disables clicks.

#pragma once

#include <functional>

#include "IControl.h"
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::IMouseMod;

class T3kPill : public IControl
{
public:
  enum class Mode { Toggle, Static };

  T3kPill(const IRECT& bounds,
          const char* label,
          Mode mode = Mode::Toggle,
          std::function<void(bool)> onToggle = nullptr);

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;

  bool isOn() const { return mOn; }
  void setOn(bool on) { mOn = on; SetDirty(false); }

private:
  const char* mLabel;
  Mode mMode;
  std::function<void(bool)> mOnToggle;
  bool mOn = false;
};

}  // namespace t3k::ui
