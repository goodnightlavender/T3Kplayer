// T3kAccountMenu — avatar-anchored dropdown for the Phase 5 auth
// surface.
//
// Same shape and pattern as T3kPresetOverlay (with the
// T3kClickBackdrop providing dismiss-on-outside-click coverage).
// Contents top-to-bottom:
//   - Header row: "Signed in as @username" (read-only label) when
//     signed in; empty/hidden otherwise.
//   - "Settings" item (no-op for Phase 5).
//   - "Dev: mock sign in as @testuser" item (Debug build only; visible
//     only when signed-out).
//   - "Sign out" item (visible when signed in).
//
// The owner (ToneRoot) wires the callbacks; the menu itself just
// renders rows and dispatches clicks. Visibility of the mock + sign-
// out rows is controlled by setItems().

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

struct AccountMenuItems {
  bool        showMockSignIn = false;
  bool        showSignOut    = true;
  std::string activeUsername;       // empty when signed out
};

class T3kAccountMenu : public IControl
{
public:
  explicit T3kAccountMenu(const IRECT& bounds);

  void setItems(AccountMenuItems items);
  const AccountMenuItems& items() const { return mItems; }

  // Callbacks — wired by the parent (ToneRoot).
  std::function<void()> onSettings;
  std::function<void()> onMockSignIn;
  std::function<void()> onSignOut;

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;

private:
  AccountMenuItems mItems;

  // Sub-rects computed from mRECT.
  IRECT headerRect()      const;
  IRECT settingsRect()    const;
  IRECT mockSignInRect()  const;
  IRECT signOutRect()     const;
};

}  // namespace t3k::ui
