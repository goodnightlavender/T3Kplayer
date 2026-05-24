// T3kSettingsModal — full-window settings panel attached by ToneRoot.
//
// Visual: dimmed backdrop + centered ~640×420 card showing:
//   - Title "Settings"
//   - TONE3000 root folder (read-only path + "CHANGE…" button)
//   - "REFRESH LIBRARY" button (kicks LibraryScanner::rescan)
//   - "SIGN OUT" button (only enabled when signed in)
//   - Worker URL (read-only label; "(not configured)" when REPLACE_ME)
//   - About block: plug-in name + version + copyright + build date
//   - "CLOSE" button (also fired by any backdrop click)
//
// ToneRoot owns the modal, attaches it last in z-order so it sits above
// the rest of the tab body, and toggles Hide() via the account menu's
// onSettings callback.

#pragma once

#include <functional>
#include <string>

#include "IControl.h"

namespace t3k::ui {

class T3kButton;

class T3kSettingsModal : public iplug::igraphics::IControl {
 public:
  using OnClose = std::function<void()>;

  T3kSettingsModal(const iplug::igraphics::IRECT& bounds, OnClose onClose);

  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnResize() override;
  void OnAttached() override;
  void OnMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) override;

  // Hide cascades to child buttons (which are flat-attached to IGraphics,
  // not parented through this control).
  void Hide(bool hide) override;

  // Refresh the visible state — call when the modal is being shown so
  // the labels reflect the latest Settings / Session state.
  void refresh();

 private:
  void pickToneRoot();
  void doSignOut();
  void doRescan();

  iplug::igraphics::IRECT mCardRect;
  iplug::igraphics::IRECT mChangeRootBtnRect;
  iplug::igraphics::IRECT mRescanBtnRect;
  iplug::igraphics::IRECT mSignOutBtnRect;
  iplug::igraphics::IRECT mCloseBtnRect;

  OnClose mOnClose;

  T3kButton* mChangeRootBtn = nullptr;
  T3kButton* mRescanBtn     = nullptr;
  T3kButton* mSignOutBtn    = nullptr;
  T3kButton* mCloseBtn      = nullptr;

  // Cached label strings so Draw doesn't have to re-fetch Settings or
  // Session each paint. refresh() repopulates these.
  std::string mRootPathLabel;
  std::string mWorkerUrlLabel;
  std::string mSignedInLabel;
  bool        mSignedIn = false;
};

}  // namespace t3k::ui
