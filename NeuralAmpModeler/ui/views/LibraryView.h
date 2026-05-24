// LibraryView.h — Library tab body in its post-Phase-10-polish form,
// modelled on the Cloud tab.
//
// Layout (1280x800 default window):
//   +- Header (search + sort) -----------------------------------+
//   |                                                            |
//   +- Sidebar -+- Card grid (6 cols x 4 visible rows) -----------+
//   | Gear      |                                                 |
//   | Tags      |   [card] [card] [card] [card] [card] [card]     |   v scroll
//   | Makes     |   [card] [card] [card] [card] [card] [card]     |
//   | Creators  |   [card] [card] [card] [card] [card] [card]     |
//   | Technical |   [card] [card] [card] [card] [card] [card]     |
//   +-----------+- Detail strip (selected card) ------------------+
//   |           | [icon] Name . Creator . NAM 12.3 MB             |
//   |           | [LOAD INTO CHAIN] [RENAME] [SHOW] [REMOVE]      |
//   +-----------+--------------------------------------------------+
//
// Sidebar: five filter accordions mirroring CloudView's set (Gear, Tags,
// Makes, Creators, Technical). Gear/Make/Creator/Format filters drive
// live re-querying against LibraryDb; Tags is still a placeholder until
// the local library carries tag metadata.
//
// Grid: each cell is a T3kLibraryCard (compact vertical tile). Cards
// are attached lazily by ensureCardCount() — we only allocate as many
// cards as are visible on screen plus one row of slack, and reuse them
// when the user scrolls (mScrollOffset is in pixels).

#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "IControl.h"

#include "../../library/LibraryDb.h"  // ModelRow

namespace t3k::ui {

class T3kSearchBar;
class T3kButton;
class T3kAccordion;
class T3kLibraryCard;
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
  void OnMouseWheel(float x, float y,
                    const iplug::igraphics::IMouseMod& mod, float d) override;
  void OnPopupMenuSelection(iplug::igraphics::IPopupMenu* pSelectedMenu,
                            int valIdx) override;

  // iPlug2 attaches all controls flat — a hidden view does NOT propagate
  // to its children. Override Hide to manually propagate so tab-switches
  // don't leak this view's child controls onto the active tab.
  void Hide(bool hide) override;

  ~LibraryView() override;

 private:
  // ── Data refresh ──────────────────────────────────────────────
  // Re-query LibraryDb and apply current filters. Repopulates mRows
  // and rebuilds the distinct-makes/creators filter sets.
  void refresh();
  // Apply the in-memory filter sets (mSelectedGears / Makes / Creators
  // / Formats) to the latest LibraryDb result. Sets mRows to the
  // visible subset. Called from refresh() and whenever a filter
  // toggles.
  void applyFilters();
  // Recompute mAllMakes / mAllCreators from mAllRows so the sidebar
  // accordions show the right options.
  void recomputeFilterOptions();

  // ── Grid management ───────────────────────────────────────────
  // Ensure exactly `n` T3kLibraryCard children exist (lazy attach,
  // never detaches — we Hide(true) extras instead). Returns the
  // running count.
  void ensureCardCount(int n);
  // Walk visible cards and (re)position / Hide them based on the
  // current mScrollOffset and the row data.
  void layoutCards();

  // ── Selection / action handlers ───────────────────────────────
  void onCardClicked(int64_t id);
  void onCardRightClicked(int64_t id, float x, float y);
  void removeSelected();
  void revealSelected();
  void loadSelected();
  void renameSelected();
  void updateDetailButtons();

  // ── Sidebar drawing (T3kAccordion content callbacks) ──────────
  void drawGearAccordion(const iplug::igraphics::IRECT& r);
  void drawTagsAccordion(const iplug::igraphics::IRECT& r);
  void drawMakesAccordion(const iplug::igraphics::IRECT& r);
  void drawCreatorsAccordion(const iplug::igraphics::IRECT& r);
  void drawTechnicalAccordion(const iplug::igraphics::IRECT& r);
  bool handleSidebarClick(float x, float y);
  void layoutSidebar();

  // Rename overlay flow.
  void showRenameOverlay(int64_t modelId);

  // Scroll helpers.
  void scrollBy(float d);
  float gridContentHeight() const;
  float gridViewportHeight() const;

  // ── Layout sub-rects (recomputed in OnResize) ──────────────────
  iplug::igraphics::IRECT mHeaderRect;
  iplug::igraphics::IRECT mSearchRect;
  iplug::igraphics::IRECT mSortRect;
  iplug::igraphics::IRECT mSidebarRect;
  iplug::igraphics::IRECT mGridRect;
  iplug::igraphics::IRECT mDetailRect;

  // Detail-strip button rects.
  iplug::igraphics::IRECT mLoadBtnRect;
  iplug::igraphics::IRECT mRenameBtnRect;
  iplug::igraphics::IRECT mRevealBtnRect;
  iplug::igraphics::IRECT mRemoveBtnRect;

  // Cached row rects for sidebar checkbox hit-testing. Filled inside
  // the accordion drawContent callbacks; consumed by handleSidebarClick.
  std::vector<std::pair<std::string, iplug::igraphics::IRECT>> mGearRowRects;
  std::vector<std::pair<std::string, iplug::igraphics::IRECT>> mMakeRowRects;
  std::vector<std::pair<std::string, iplug::igraphics::IRECT>> mCreatorRowRects;
  std::vector<std::pair<std::string, iplug::igraphics::IRECT>> mFormatRowRects;

  // ── Children ──────────────────────────────────────────────────
  T3kSearchBar*    mSearchBar    = nullptr;
  T3kButton*       mRescanBtn    = nullptr;
  T3kAccordion*    mGearAcc      = nullptr;
  T3kAccordion*    mTagsAcc      = nullptr;
  T3kAccordion*    mMakesAcc     = nullptr;
  T3kAccordion*    mCreatorsAcc  = nullptr;
  T3kAccordion*    mTechAcc      = nullptr;
  T3kRenameOverlay* mRenameOverlay = nullptr;
  // Grid cards. Owned by IGraphics; we only hold raw pointers.
  std::vector<T3kLibraryCard*> mCards;
  // Detail-strip action buttons.
  T3kButton*       mLoadBtn      = nullptr;
  T3kButton*       mRenameBtn    = nullptr;
  T3kButton*       mRevealBtn    = nullptr;
  T3kButton*       mRemoveBtn    = nullptr;

  // ── Data ──────────────────────────────────────────────────────
  std::vector<::t3k::library::ModelRow> mAllRows;  // raw LibraryDb result
  std::vector<::t3k::library::ModelRow> mRows;     // post-filter

  // Distinct values discovered in mAllRows. Populated by
  // recomputeFilterOptions().
  std::vector<std::string> mAllMakes;
  std::vector<std::string> mAllCreators;

  // Filter state.
  std::unordered_set<std::string> mSelectedGears;     // pedal/amp/cab/...
  std::unordered_set<std::string> mSelectedMakes;
  std::unordered_set<std::string> mSelectedCreators;
  std::unordered_set<std::string> mSelectedFormats;   // "nam" / "ir"

  // Search query (debounce handled by T3kSearchBar).
  std::string mSearch;

  // EventBus subscription token (so we can unsubscribe in dtor).
  int mBusToken = 0;

  // Selection state — the currently-focused card. 0 = nothing selected.
  int64_t mSelectedId = 0;

  // Right-click context state.
  int64_t mCtxModelId      = 0;
  int64_t mRenameTargetId  = 0;
  int64_t mRemoveConfirmId = 0;

  // Grid scroll offset, in pixels (always >= 0).
  float mScrollOffset = 0.f;

  OnModelClicked mOnModelClicked;
};

}  // namespace t3k::ui
