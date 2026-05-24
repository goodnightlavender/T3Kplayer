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

  struct DetailData {
    std::string title;
    std::string subtitle;            // gear-type . variant count etc. (accent)
    std::string creator;
    std::string description;         // wrapped paragraph
    std::vector<std::string> makesModels;  // bullet list
    std::vector<std::string> tags;         // pill chips
    std::string imagePath;           // local FS path; preferred
    std::string imageUrl;            // remote URL (cloud); resolved via
                                     // cloud::ThumbnailCache when set.
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
  void Hide(bool hide) override;

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
  T3kButton* mCloseBtn = nullptr;

  // Lazy bitmap cache for mData.imagePath.
  bool mBitmapLoaded     = false;
  bool mBitmapLoadFailed = false;
  iplug::igraphics::IBitmap mBitmap;
};

}  // namespace t3k::ui
