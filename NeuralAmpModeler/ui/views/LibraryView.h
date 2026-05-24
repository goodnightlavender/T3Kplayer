// LibraryView.h — Phase 10 reimagining of the Library tab body.
//
// Layout: header (search + Rescan) on top; below that the body is split
// into a virtual list (~70% of width) on the left and a fixed-width
// detail pane (~300px) on the right. Selecting a row in the list fills
// the detail pane and reveals action buttons (Load, Rename, Remove,
// Show in Explorer). The legacy "TEST NET" button has been retired —
// Phase 4 brought the network online, the smoke-test surface no longer
// earns its slot in the header chrome.
//
// LibraryView still OWNS the scroll list (T3kVScrollList) and renders
// each row inline via T3kVScrollList's draw-callback; the rows are
// taller now (~88px) and carry a thumbnail tile on the left, so the
// virtualization budget stays the same regardless of library size.

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

  // iPlug2 attaches all controls flat — a hidden view does NOT propagate
  // to its children. Override Hide to manually propagate so tab-switches
  // don't leak this view's child controls onto the active tab.
  void Hide(bool hide) override;

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

  // Remove `mSelectedId` from LibraryDb + nuke the on-disk files. Called
  // from the detail-pane Remove button and the popup-menu Remove item
  // after the user confirms via a two-step "Remove → Confirm" popup.
  void removeSelected();

  // Open the OS file explorer focused on `mSelectedId`'s on-disk path.
  void revealSelected();

  // Stage `mSelectedId` into the chain (delegates to mOnModelClicked).
  void loadSelected();

  // Refresh the detail-pane button visibility based on whether a row
  // is selected. The detail pane stays visible always; the buttons
  // disappear when nothing is selected.
  void updateDetailButtons();

  // Header chrome rects, recomputed in OnResize.
  iplug::igraphics::IRECT mHeaderRect;
  iplug::igraphics::IRECT mSearchRect;
  iplug::igraphics::IRECT mRescanRect;
  iplug::igraphics::IRECT mBodyRect;        // header.B → mRECT.B
  iplug::igraphics::IRECT mListRect;        // left side of body
  iplug::igraphics::IRECT mDetailRect;      // right side of body

  // Detail-pane child button rects.
  iplug::igraphics::IRECT mLoadBtnRect;
  iplug::igraphics::IRECT mRenameBtnRect;
  iplug::igraphics::IRECT mRevealBtnRect;
  iplug::igraphics::IRECT mRemoveBtnRect;

  // Children. Owned by IGraphics after AttachControl.
  T3kSearchBar*     mSearchBar       = nullptr;
  T3kButton*        mRescanBtn       = nullptr;
  T3kVScrollList*   mScrollList      = nullptr;
  T3kRenameOverlay* mRenameOverlay   = nullptr;
  // Detail-pane action buttons.
  T3kButton*        mLoadBtn         = nullptr;
  T3kButton*        mRenameBtn       = nullptr;
  T3kButton*        mRevealBtn       = nullptr;
  T3kButton*        mRemoveBtn       = nullptr;

  // Data backing the list.
  std::vector<::t3k::library::ModelRow> mRows;

  // Search query (debounce handled by T3kSearchBar itself).
  std::string mSearch;

  // EventBus subscription token (so we can unsubscribe in dtor).
  int mBusToken = 0;

  // Selection state — the currently-focused row in the list. Drives the
  // detail pane and the action buttons. 0 = nothing selected.
  int64_t mSelectedId = 0;

  // Right-click context state — set in OnMouseDown when a right-click
  // hits a row, consumed by OnPopupMenuSelection.
  int64_t mCtxModelId = 0;

  // The row currently being renamed (so we can apply the new name when
  // T3kRenameOverlay fires its callback).
  int64_t mRenameTargetId = 0;

  // Two-step popup-menu remove confirmation. When the user picks
  // "Remove from library…" we open a second popup menu that asks
  // "Confirm: delete <name>?" — we remember which model id we're
  // about to wipe between the two popup roundtrips here.
  int64_t mRemoveConfirmId = 0;

  OnModelClicked mOnModelClicked;
};

}  // namespace t3k::ui
