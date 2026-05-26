// T3kDetailModal — full-window detail page for a tone (Library or
// Cloud). Modelled after tone3000.com's tone detail page:
//
//   +-----------------------------------------------------------+
//   |                                                       [X] |
//   |  +-----------+    Big bold title                          |
//   |  |           |    Subtitle (accent colour)                |
//   |  |   image   |                                            |
//   |  |  280x280  |    o-avatar  Creator name                  |
//   |  |  or icon  |    -------------------------------------   |
//   |  +-----------+    Description                             |
//   |                   Body paragraph wrapping...              |
//   |                                                           |
//   |                   Makes and Models                        |
//   |                    . Item 1                               |
//   |                    . Item 2                               |
//   |                                                           |
//   |                   Tags                                    |
//   |                   ( tag1 ) ( tag2 )                       |
//   |                                                           |
//   |                       [ ACTION ] [ ACTION ] [ ACTION ]    |
//   +-----------------------------------------------------------+
//
// Reusable: callers fill a DetailData + a vector<Action>. Library
// shows Load / Rename / Show / Remove; Cloud shows Download.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "IControl.h"
#include "IGraphicsStructs.h"

namespace t3k::ui {

class T3kButton;

class T3kDetailModal : public iplug::igraphics::IControl {
 public:
  using OnClose = std::function<void()>;

  // A row in the "Versions" / pickables list. Rendered as a label
  // with a small PICK button on the right. Used by the Library
  // detail page so the user can load a specific variant.
  struct PickableItem {
    std::string label;
    std::function<void()> onPick;
    std::function<void(const std::string&)> onRename;
  };

  struct DetailData {
    std::string title;
    std::string subtitle;            // gear-type . variant count etc. (accent)
    std::string creator;
    std::string description;         // wrapped paragraph
    std::vector<std::string> makesModels;  // descriptive bullet list (Cloud)
    std::vector<PickableItem> pickables;   // when non-empty, replaces
                                           // the makesModels section
                                           // with a list of PICK buttons.
    std::vector<std::string> tags;         // pill chips
    std::string imagePath;           // local FS path; preferred
    std::string imageUrl;            // remote URL (cloud); resolved via
                                     // cloud::ThumbnailCache when set.
    std::function<void(const std::string&)> onEditDescription;
    std::function<void(const std::string&)> onEditTitle;
  };

  struct Action {
    std::string label;
    bool primary = false;
    std::function<void()> onClick;
  };

  T3kDetailModal(const iplug::igraphics::IRECT& bounds, OnClose onClose);

  // Populate + show. Detaches the previous Action buttons and attaches
  // a fresh set so each invocation can carry its own behaviour. The
  // modal also unhides itself.
  void show(DetailData data, std::vector<Action> actions);

  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnResize() override;
  void OnAttached() override;
  void OnMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) override;
  void OnMouseDblClick(float x, float y, const iplug::igraphics::IMouseMod& mod) override;
  // 2026-05-25 — mouse-wheel scroll for the Versions list. Previously
  // the list was capped at 6 visible variants with a "+N more..."
  // hint and the rest were unreachable. Now the list scrolls.
  void OnMouseWheel(float x, float y,
                    const iplug::igraphics::IMouseMod& mod,
                    float d) override;
  // 2026-05-25 — drag the scrollbar thumb to scroll the Versions
  // list. OnMouseDown starts a drag when the thumb is clicked; this
  // updates the offset as the mouse moves; OnMouseUp ends the drag.
  void OnMouseDrag(float x, float y, float dX, float dY,
                   const iplug::igraphics::IMouseMod& mod) override;
  void OnMouseUp(float x, float y,
                 const iplug::igraphics::IMouseMod& mod) override;
  void OnTextEntryCompletion(const char* str, int valIdx) override;
  void Hide(bool hide) override;

  // Remove flat-attached children (close button, action buttons) from
  // IGraphics. Call this BEFORE the owning view does
  // g->RemoveControl(modal) so the buttons don't dangle. Used by the
  // "recreate on top" z-order pattern.
  void detachAllChildren();

 private:
  void recomputeLayout();
  void rebuildActionButtons();
  // Wraps `text` into lines fitting `widthPx` using a space-split
  // estimator (NanoVG doesn't expose synchronous measureText through
  // iPlug2's IGraphics). Approximate but stable.
  std::vector<std::string> wrapText(const std::string& text,
                                    float widthPx, float pxPerChar) const;

  iplug::igraphics::IRECT mCardRect;
  iplug::igraphics::IRECT mImageRect;
  iplug::igraphics::IRECT mTextRect;
  iplug::igraphics::IRECT mCloseBtnRect;

  OnClose mOnClose;

  DetailData mData;
  std::vector<Action> mActions;

  // Action buttons — owned by IGraphics; we just hold raw pointers so
  // we can rebuild them when show() is called again.
  std::vector<T3kButton*> mActionBtns;
  // Pickable variant buttons — one per DetailData::pickables entry.
  // Rebuilt each show() like mActionBtns.
  std::vector<T3kButton*> mPickBtns;
  T3kButton* mCloseBtn = nullptr;

  // Lazy bitmap cache for mData.imagePath (or the ThumbnailCache
  // resolved path when only imageUrl is set).
  bool mBitmapLoaded     = false;
  bool mBitmapLoadFailed = false;
  iplug::igraphics::IBitmap mBitmap;
  bool        mThumbRequested  = false;
  std::string mThumbPath;
  bool        mThumbLoadFailed = false;

  // 2026-05-25 — scroll state for the Versions / pickables list.
  // mPickablesScrollOffset is the pixel offset (>= 0) of the top of
  // the displayed area into the virtual stack of variants. The area
  // rect and content height are recomputed each Draw and re-used by
  // OnMouseWheel to bound the offset.
  float                   mPickablesScrollOffset = 0.f;
  iplug::igraphics::IRECT mPickablesAreaRect;
  float                   mPickablesContentHeight = 0.f;
  iplug::igraphics::IRECT mDescriptionEditRect;
  iplug::igraphics::IRECT mTitleEditRect;
  std::vector<iplug::igraphics::IRECT> mPickableLabelRects;

  // 2026-05-25 — scrollbar drag state. mScrollbarThumbRect is the
  // thumb's screen-space rect computed each Draw (used by mouse hit-
  // tests); mScrollbarDragging tracks whether the user is currently
  // pressing-and-dragging the thumb; mScrollbarDragGrabY records the
  // y-offset between the mouse-down position and the thumb's top so
  // the drag feels anchored (the thumb doesn't snap-jump to the
  // cursor on first move).
  iplug::igraphics::IRECT mScrollbarThumbRect;
  bool                    mScrollbarDragging = false;
  float                   mScrollbarDragGrabY = 0.f;

  // 2026-05-26 — content-drag state. When the user mouse-downs inside
  // the variants area but NOT on the scrollbar thumb/track (and not on
  // a PICK button — those are children and iPlug2 routes their clicks
  // separately), they enter a "drag the content" mode: dragging up
  // scrolls down (increases offset), dragging down scrolls up. Mirrors
  // touch-scroll UX. Cleared on OnMouseUp.
  bool                    mContentDragging = false;

  enum class EditTarget { None, Title, Description, Pickable };
  EditTarget mEditTarget = EditTarget::None;
  int mEditPickableIndex = -1;
};

}  // namespace t3k::ui
