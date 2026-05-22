// T3kButton — pill-shaped button with Primary / Secondary variants.
//
// Primary  : solid kAccent fill, white text. Hover lightens fill ~10%.
// Secondary: transparent fill, 1px kBorder outline, white text.
//            Hover switches the outline to kBorderActive.

#pragma once

#include <functional>

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
  enum class Variant { Primary, Secondary };

  T3kButton(const IRECT& bounds,
            const char* label,
            std::function<void()> onClick,
            Variant variant = Variant::Primary);

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const IMouseMod& mod) override;
  void OnMouseOut() override;

private:
  const char* mLabel;
  std::function<void()> mOnClick;
  Variant mVariant;
};

}  // namespace t3k::ui
