// T3kAccountMenu implementation. See T3kAccountMenu.h.

#include "T3kAccountMenu.h"

#include <string>

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// Layout constants — mirrors T3kPresetOverlay's panel chrome so the
// two dropdowns feel like siblings in the header.
constexpr float kPanelPad   = 8.f;
constexpr float kHeaderH    = 28.f;   // "Signed in as @user" label
constexpr float kRowH       = 26.f;   // each clickable item
constexpr float kDividerH   = 1.f;

const IColor kPanelBg     {255,  12,  12,  12};  // #0c0c0c
const IColor kPanelBorder {255,  31,  31,  31};  // #1f1f1f

}  // namespace

T3kAccountMenu::T3kAccountMenu(const IRECT& bounds)
: IControl(bounds)
{
}

void T3kAccountMenu::setItems(AccountMenuItems items)
{
  mItems = std::move(items);
  SetDirty(false);
}

IRECT T3kAccountMenu::headerRect() const
{
  // The header band only renders content when signed-in (so the
  // "Signed in as @user" label has something to say). When signed-out
  // we still reserve the slot so the divider lines up below it.
  return IRECT(mRECT.L + kPanelPad,
               mRECT.T + kPanelPad,
               mRECT.R - kPanelPad,
               mRECT.T + kPanelPad + kHeaderH);
}

IRECT T3kAccountMenu::settingsRect() const
{
  const float top = headerRect().B + kDividerH + 4.f;
  return IRECT(mRECT.L + kPanelPad, top,
               mRECT.R - kPanelPad, top + kRowH);
}

IRECT T3kAccountMenu::mockSignInRect() const
{
  // Mock sign-in slot is shown when signed-out (in Debug builds).
  // Lives directly under the Settings row.
  const float top = settingsRect().B;
  return IRECT(mRECT.L + kPanelPad, top,
               mRECT.R - kPanelPad, top + kRowH);
}

IRECT T3kAccountMenu::signOutRect() const
{
  // Sign out lives at the bottom regardless of whether mock is shown.
  return IRECT(mRECT.L + kPanelPad,
               mRECT.B - kPanelPad - kRowH,
               mRECT.R - kPanelPad,
               mRECT.B - kPanelPad);
}

void T3kAccountMenu::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Panel chrome.
  g.FillRoundRect(kPanelBg, mRECT, th::kRadiusLg);
  g.DrawRoundRect(kPanelBorder, mRECT, th::kRadiusLg, nullptr, 1.f);

  // ── Header band ────────────────────────────────────────────────
  const IRECT hr = headerRect();
  if (!mItems.activeUsername.empty()) {
    const std::string label = "Signed in as @" + mItems.activeUsername;
    g.DrawText(IText(th::kTypeSmall, th::kText,
                     th::kFontBodyMed, EAlign::Near, EVAlign::Middle),
               label.c_str(),
               IRECT(hr.L + 4.f, hr.T, hr.R, hr.B));
  } else {
    // Signed-out greeting — keeps the menu from collapsing visually.
    g.DrawText(IText(th::kTypeSmall, th::kTextMuted,
                     th::kFontBody, EAlign::Near, EVAlign::Middle),
               "Not signed in",
               IRECT(hr.L + 4.f, hr.T, hr.R, hr.B));
  }

  // Divider beneath the header.
  g.FillRect(kPanelBorder,
             IRECT(mRECT.L + kPanelPad, hr.B,
                   mRECT.R - kPanelPad, hr.B + kDividerH));

  auto drawRow = [&](const IRECT& r, const char* label,
                     const IColor& textCol) {
    g.DrawText(IText(th::kTypeSmall, textCol,
                     th::kFontBody, EAlign::Near, EVAlign::Middle),
               label,
               IRECT(r.L + 4.f, r.T, r.R, r.B));
  };

  // ── Settings row ───────────────────────────────────────────────
  drawRow(settingsRect(), "Settings", th::kTextMuted);

  // ── Dev mock sign-in (only when explicitly requested) ─────────
  if (mItems.showMockSignIn) {
    drawRow(mockSignInRect(),
            "Dev: mock sign in as @testuser",
            th::kText);
  }

  // ── Sign out ───────────────────────────────────────────────────
  if (mItems.showSignOut) {
    drawRow(signOutRect(), "Sign out", th::kText);
  }
}

void T3kAccountMenu::OnMouseDown(float x, float y, const IMouseMod& /*mod*/)
{
  if (settingsRect().Contains(x, y)) {
    if (onSettings) onSettings();
    SetDirty(false);
    return;
  }
  if (mItems.showMockSignIn && mockSignInRect().Contains(x, y)) {
    if (onMockSignIn) onMockSignIn();
    SetDirty(false);
    return;
  }
  if (mItems.showSignOut && signOutRect().Contains(x, y)) {
    if (onSignOut) onSignOut();
    SetDirty(false);
    return;
  }
}

}  // namespace t3k::ui
