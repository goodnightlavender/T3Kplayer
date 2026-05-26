// T3kButton — pill-shaped button with Primary / Secondary / Invert variants.
//
// Primary  : solid kAccent fill, white text. Hover lightens fill ~10%.
// Secondary: transparent fill, 1px kBorder outline, white text.
//            Hover switches the outline to kBorderActive.
// Invert   : solid white fill, black text. Used for high-affordance
//            CTAs on top of the accent (since the accent is now yellow,
//            white-on-accent is unreadable — Invert keeps the "loud
//            primary" feel without the contrast collision).

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

class T3kButton : public IControl
{
public:
  enum class Variant { Primary, Secondary, Invert };

  T3kButton(const IRECT& bounds,
            const char* label,
            std::function<void()> onClick,
            Variant variant = Variant::Primary);

  // Swap the label after construction. Used by the Cloud-tab sort-cycle
  // button which steps through "Best match" / "Newest" / "Trending" /
  // etc. on each click. The stored std::string owns the buffer so the
  // caller doesn't have to keep a const char* alive.
  void setLabel(const char* label);

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const IMouseMod& mod) override;
  void OnMouseOut() override;

private:
  // Owns the label buffer so callers can pass a temporary const char*
  // or an std::string body without lifetime concerns.
  std::string mLabel;
  std::function<void()> mOnClick;
  Variant mVariant;
};

}  // namespace t3k::ui
