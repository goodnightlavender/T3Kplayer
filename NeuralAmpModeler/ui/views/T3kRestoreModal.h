// T3kRestoreModal — full-window overlay shown after a successful
// LibrarySync pull discovers N tones in the user's cloud library
// that aren't on this device.
//
// Visual: dimmed backdrop + centered ~520×260 card with:
//   - Title "Restore your library?"
//   - Body line: "<N> tones from your TONE3000 library aren't on
//                this device. Redownload them now?"
//   - Two buttons:
//       Primary  : "Restore all"
//       Secondary: "Not now"
//
// ToneRoot owns the modal, attaches it last in z-order so it sits
// above every other tab/control, and toggles Hide() based on
// LibrarySync pull events.

#pragma once

#include <functional>

#include "IControl.h"

namespace t3k::ui {

class T3kButton;

class T3kRestoreModal : public iplug::igraphics::IControl {
 public:
  using OnChoice = std::function<void()>;

  T3kRestoreModal(const iplug::igraphics::IRECT& bounds,
                  OnChoice onRestore,
                  OnChoice onDismiss);

  // Update the missing-count text. Safe to call repeatedly — the
  // next paint cycle picks up the new value.
  void setMissingCount(int n);

  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnResize() override;
  void OnAttached() override;

  // Hide cascades to child buttons (which are attached flat to
  // IGraphics, not parented through this control).
  void Hide(bool hide) override;

 private:
  iplug::igraphics::IRECT mCardRect;
  iplug::igraphics::IRECT mPrimaryBtnRect;
  iplug::igraphics::IRECT mSecondaryBtnRect;

  OnChoice mOnRestore;
  OnChoice mOnDismiss;

  int mMissingCount = 0;

  T3kButton* mRestoreBtn = nullptr;
  T3kButton* mDismissBtn = nullptr;
};

}  // namespace t3k::ui
