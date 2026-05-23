// T3kSignInPill — header-right "Sign in" pill (Phase 5).
//
// Replaces the avatar circle when cloud::Session is SignedOut. Click
// triggers the OAuth flow (which the parent wires by passing
// Session::signIn() into onClick — or by routing to the account menu
// for the dev-mock path).
//
// Visual: ~80px wide × 24px tall, kAccent fill, white "Sign in" label
// in Inter SemiBold. Hover slightly lightens the fill to read
// affordance — same treatment as T3kButton::Primary.

#pragma once

#include <functional>

#include "IControl.h"
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::IMouseMod;

class T3kSignInPill : public IControl
{
public:
  T3kSignInPill(const IRECT& bounds, std::function<void()> onClick);

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const IMouseMod& mod) override;
  void OnMouseOut() override;

private:
  std::function<void()> mOnClick;
};

}  // namespace t3k::ui
