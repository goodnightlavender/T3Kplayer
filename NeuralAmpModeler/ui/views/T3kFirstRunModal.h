// T3kFirstRunModal — full-window overlay shown when settings.tone3000_root
// is empty.
//
// Visual: dimmed backdrop over the rest of the window, centered card
// (~480×320) with title "Welcome to TONE3000 Player", explanatory body
// text, the suggested path (kTextDim), and two buttons:
//   - "Use suggested folder"  (Primary)
//   - "Pick custom…"          (Secondary, opens IGraphics::PromptForDirectory)
//
// When the user makes a choice, fires onPicked(path) and the modal is
// removed from the screen by the parent (ToneRoot owns and hides it).

#pragma once

#include <functional>
#include <string>

#include "IControl.h"

namespace t3k::ui {

class T3kButton;

class T3kFirstRunModal : public iplug::igraphics::IControl {
 public:
  using OnPicked = std::function<void(const std::string& chosenRoot)>;

  T3kFirstRunModal(const iplug::igraphics::IRECT& bounds, OnPicked onPicked);

  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnResize() override;
  void OnAttached() override;
  // OnMouseDown intentionally not overridden — the buttons are real
  // T3kButton children attached during OnAttached.

 private:
  // Layout sub-rects, recomputed in OnResize.
  iplug::igraphics::IRECT mCardRect;
  iplug::igraphics::IRECT mPrimaryBtnRect;
  iplug::igraphics::IRECT mSecondaryBtnRect;

  OnPicked  mOnPicked;

  // Cached suggested path string (computed once in ctor). Held alive
  // because we draw it inside Draw().
  std::string mSuggestedPath;

  // Child buttons. Owned by IGraphics after AttachControl in OnAttached.
  T3kButton* mUseSuggestedBtn = nullptr;
  T3kButton* mPickCustomBtn   = nullptr;

  // Handlers — bound to the buttons in OnAttached.
  void onUseSuggested();
  void onPickCustom();
};

}  // namespace t3k::ui
