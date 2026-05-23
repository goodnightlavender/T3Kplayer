// LibraryView.h — Phase 3 implementation of the Library tab body.
//
// Composite control: search bar + Rescan button (chrome) plus a virtual
// vertical list of model rows drawn by the LibraryView itself. Each row
// shows the model's effective display name, the creator, and the file
// size; right-click on a row opens a popup menu with "Rename" which
// positions a shared T3kRenameOverlay below the row.
//
// LibraryView OWNS the scroll list (T3kVScrollList) but draws each row
// inline via T3kVScrollList's draw-callback rather than attaching one
// IControl per row. This keeps the control-list shallow (a 1000-model
// library would otherwise need 1000 IControls) and lets the right-click
// → rename flow work without forwarding mouse events through child
// indirection.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "IControl.h"

#include "../../library/LibraryDb.h"  // ModelRow

namespace t3k::ui {

class T3kSearchBar;
class T3kButton;
class T3kVScrollList;
class T3kRenameOverlay;

class LibraryView : public iplug::igraphics::IControl {
 public:
  using OnModelClicked = std::function<void(const std::string& toneId,
                                             const std::string& modelId)>;

  LibraryView(const iplug::igraphics::IRECT& bounds,
              OnModelClicked onClick = {});

  void setOnModelClicked(OnModelClicked cb) { mOnModelClicked = std::move(cb); }

  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnResize() override;
  void OnAttached() override;
  void OnMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) override;
  void OnPopupMenuSelection(iplug::igraphics::IPopupMenu* pSelectedMenu,
                            int valIdx) override;

  ~LibraryView() override;

 private:
  // Re-run LibraryDb::queryByName(mSearch) and stash the result. Called
  // from OnAttached, EventBus listeners, and the debounce timer.
  void refresh();

  // Drop the rename overlay back to invisible.
  void hideRenameOverlay();

  // Show the rename overlay anchored under the row whose model is `id`.
  void showRenameOverlay(int64_t modelId);

  // Find a row by row-id. Returns -1 if not visible.
  int findRowIndexById(int64_t modelId) const;

  // Header chrome rects, recomputed in OnResize.
  iplug::igraphics::IRECT mHeaderRect;
  iplug::igraphics::IRECT mSearchRect;
  iplug::igraphics::IRECT mRescanRect;
  iplug::igraphics::IRECT mListRect;

  // Children. Owned by IGraphics after AttachControl.
  T3kSearchBar*     mSearchBar       = nullptr;
  T3kButton*        mRescanBtn       = nullptr;
  T3kVScrollList*   mScrollList      = nullptr;
  T3kRenameOverlay* mRenameOverlay   = nullptr;

  // Data backing the list.
  std::vector<::t3k::library::ModelRow> mRows;

  // Debounced search.
  std::string mSearch;
  std::string mPendingSearch;
  std::chrono::steady_clock::time_point mPendingSince{};

  // EventBus subscription token (so we can unsubscribe in dtor).
  int mBusToken = 0;

  // Right-click context state — set in OnMouseDown when a right-click
  // hits a row, consumed by OnPopupMenuSelection.
  int64_t mCtxModelId = 0;

  // The row currently being renamed (so we can apply the new name when
  // T3kRenameOverlay fires its callback).
  int64_t mRenameTargetId = 0;

  OnModelClicked mOnModelClicked;
};

}  // namespace t3k::ui
