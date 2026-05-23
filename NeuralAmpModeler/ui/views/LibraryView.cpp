// LibraryView.cpp — Phase 3 Library tab body. Replaces the Phase 2
// placeholder.
//
// See LibraryView.h for architecture notes. Key implementation choices:
//   - We virtualize row drawing via T3kVScrollList's draw callback so
//     a 1000-row library doesn't construct 1000 IControl children.
//   - The scroll list's draw callback paints into a pre-computed
//     itemRect — we render display name + creator + size inline.
//   - Right-click goes through LibraryView::OnMouseDown rather than
//     each row. We hit-test rows using the scroll list's reported
//     first/last visible indices.
//   - Search debounce: T3kSearchBar's CreateTextEntry commits on
//     Enter / focus-loss only (per its FIXME), so there's nothing to
//     debounce at this revision — we re-query on each commit.
//   - The T3kRenameOverlay control (Task 9) is referenced via a
//     forward-declared pointer here and intentionally unused; Task 9
//     wires the showRenameOverlay path. The popup-menu entry still
//     stores mRenameTargetId so the wiring is testable in isolation
//     once T3kRenameOverlay lands.

#include "LibraryView.h"

#include <algorithm>
#include <cstdio>

#include "IGraphics.h"

#include "../theme.h"
#include "../layout.h"
#include "../controls/T3kButton.h"
#include "../controls/T3kRenameOverlay.h"
#include "../controls/T3kSearchBar.h"
#include "../controls/T3kVScrollList.h"
#include "../../library/EventBus.h"
#include "../../library/LibraryScanner.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// Header row dimensions (search bar + Rescan button).
constexpr float kHeaderH       = 56.f;
constexpr float kHeaderPad     = 16.f;
constexpr float kRescanBtnW    = 96.f;
constexpr float kRescanBtnH    = 28.f;
constexpr float kSearchH       = 32.f;

// Row metrics. We omit the image until Phase 7 lands real thumbnail
// downloads — the design has an 88px tile but with no downloaded
// images yet it would always be the placeholder gradient, which Phase
// 2b's T3kCard already covers in the Cloud tab. Keep rows lean here.
constexpr float kRowH          = 56.f;
constexpr float kRowPadL       = 16.f;
constexpr float kRowPadR       = 16.f;

// Popup menu commands.
enum PopupCmd { kCmdNone = 0, kCmdRename = 1 };

// Format size_bytes as a human-readable string.
std::string FormatSize(int64_t bytes)
{
  constexpr int64_t kMb = 1024 * 1024;
  constexpr int64_t kKb = 1024;
  char buf[64];
  if (bytes >= kMb) {
    const double mb = double(bytes) / double(kMb);
    std::snprintf(buf, sizeof(buf), "%.1f MB", mb);
  } else if (bytes >= kKb) {
    std::snprintf(buf, sizeof(buf), "%lld KB",
                  static_cast<long long>(bytes / kKb));
  } else {
    std::snprintf(buf, sizeof(buf), "%lld B", static_cast<long long>(bytes));
  }
  return buf;
}

}  // namespace

LibraryView::LibraryView(const IRECT& bounds, OnModelClicked onClick)
: IControl(bounds)
, mOnModelClicked(std::move(onClick))
{
  OnResize();
}

LibraryView::~LibraryView()
{
  if (mBusToken > 0) {
    ::t3k::library::EventBus::instance().unsubscribe(mBusToken);
    mBusToken = 0;
  }
}

void LibraryView::OnResize()
{
  // Header row at the top, list fills the rest.
  auto split = t3k::layout::rowFixedTop(mRECT, kHeaderH);
  mHeaderRect = split.first;
  mListRect   = split.second;

  // Lay out the search bar (flex) + Rescan button (fixed width).
  const float left  = mHeaderRect.L + kHeaderPad;
  const float right = mHeaderRect.R - kHeaderPad;
  const float midY  = mHeaderRect.MH();
  const float searchTop = midY - kSearchH * 0.5f;
  const float btnTop    = midY - kRescanBtnH * 0.5f;

  mRescanRect = IRECT(right - kRescanBtnW, btnTop,
                      right,               btnTop + kRescanBtnH);
  mSearchRect = IRECT(left,                 searchTop,
                      mRescanRect.L - t3k::theme::kS3,
                      searchTop + kSearchH);

  if (mSearchBar)  mSearchBar ->SetTargetAndDrawRECTs(mSearchRect);
  if (mRescanBtn)  mRescanBtn ->SetTargetAndDrawRECTs(mRescanRect);
  if (mScrollList) mScrollList->SetTargetAndDrawRECTs(mListRect);
}

void LibraryView::OnAttached()
{
  IGraphics* g = GetUI();
  if (!g) return;

  mSearchBar = new T3kSearchBar(mSearchRect,
      [this](const std::string& v) {
        mSearch = v;
        refresh();
      },
      "Search by name or creator\xE2\x80\xA6");
  g->AttachControl(mSearchBar);

  mRescanBtn = new T3kButton(mRescanRect, "RESCAN",
      [this]() { ::t3k::library::LibraryScanner::instance().rescan(); },
      T3kButton::Variant::Secondary);
  g->AttachControl(mRescanBtn);

  mScrollList = new T3kVScrollList(mListRect,
      /*itemCount*/ [this]() { return static_cast<int>(mRows.size()); },
      /*itemHeight*/ [](int) { return kRowH; },
      /*drawItem*/ [this](int i, const IRECT& r) {
        IGraphics* gg = this->GetUI();
        if (!gg || i < 0 || i >= static_cast<int>(mRows.size())) return;
        namespace th = ::t3k::theme;
        const auto& row = mRows[i];

        // Alternating-row tint for readability.
        if (i % 2 == 1) {
          gg->FillRect(th::kBgSurface, r);
        }

        // Display name (left, 16px Inter Medium).
        const IRECT nameR(r.L + kRowPadL, r.T + 8.f,
                          r.R - kRowPadR, r.T + 8.f + 18.f);
        gg->DrawText(IText(th::kTypeH2, th::kText,
                           th::kFontBodyMed, EAlign::Near, EVAlign::Top),
                     row.effectiveDisplayName().c_str(), nameR);

        // Creator / size / gear_type — separated by " · ".
        const IRECT metaR(r.L + kRowPadL, nameR.B + 2.f,
                          r.R - kRowPadR, r.B - 8.f);
        std::string meta;
        if (!row.t3k_creator.empty()) meta = row.t3k_creator + " \xC2\xB7 ";
        meta += FormatSize(row.size_bytes);
        if (!row.gear_type.empty())   meta += " \xC2\xB7 " + row.gear_type;
        gg->DrawText(IText(th::kTypeSmall, th::kTextMuted,
                           th::kFontBody, EAlign::Near, EVAlign::Top),
                     meta.c_str(), metaR);

        // Row separator.
        gg->FillRect(th::kBorder, IRECT(r.L, r.B - 1.f, r.R, r.B));
      });
  g->AttachControl(mScrollList);

  // Shared rename overlay — hidden until showRenameOverlay positions it
  // and Hide(false)s it. Attaching last keeps it on top of the scroll
  // list in z-order.
  mRenameOverlay = new T3kRenameOverlay(IRECT(0.f, 0.f, 1.f, 1.f));
  mRenameOverlay->setOnSave([this](const std::string& newName) {
    if (mRenameTargetId <= 0) return;
    ::t3k::library::LibraryDb::instance().setDisplayNameOverride(
        mRenameTargetId,
        newName.empty() ? std::optional<std::string>(std::nullopt)
                        : std::optional<std::string>(newName));
    mRenameTargetId = 0;
    refresh();
  });
  g->AttachControl(mRenameOverlay);

  // Subscribe to scanner events — refresh the list when the scan
  // adds/removes models.
  mBusToken = ::t3k::library::EventBus::instance().subscribe(
      [this](::t3k::library::LibraryEvent ev, int64_t /*payload*/) {
        switch (ev) {
          case ::t3k::library::LibraryEvent::ModelAdded:
          case ::t3k::library::LibraryEvent::ModelUpdated:
          case ::t3k::library::LibraryEvent::ModelRemoved:
          case ::t3k::library::LibraryEvent::ScanFinished:
            refresh();
            break;
          default: break;
        }
      });

  refresh();
}

void LibraryView::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Body background + 1px header divider.
  g.FillRect(th::kBgBase, mRECT);
  g.FillRect(th::kBorder,
             IRECT(mRECT.L, mHeaderRect.B - 1.f, mRECT.R, mHeaderRect.B));

  // Empty state hint when the list is empty.
  if (mRows.empty()) {
    const char* msg = mSearch.empty()
        ? "No models yet. Drop .nam files (with their .tone3000.json sidecars) "
          "into your TONE3000 folder and hit Rescan."
        : "No models match the current search.";
    g.DrawText(IText(th::kTypeBody, th::kTextMuted,
                     th::kFontBody, EAlign::Center, EVAlign::Middle),
               msg, mListRect);
  }
}

void LibraryView::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  if (!mListRect.Contains(x, y)) return;
  if (!mScrollList) return;

  // Hit-test against the visible band reported by the scroll list's
  // most recent paint. row `first` sits at its computed top
  // (mListRect.T + (first*kRowH - scrollOffset)). Since scroll_offset
  // isn't exposed, we approximate by iterating from `first` with rect
  // tops at `mListRect.T + (i - first) * kRowH`. Phase 3 lives with
  // the rounding (rows are 56px tall and scroll is 40px/wheel-notch
  // so worst-case error is ≤ kRowH which we sandwich with floor).
  //
  // FIXME(Phase 3.5): expose scrollOffset() on T3kVScrollList for
  // pixel-perfect hit testing.
  const int first = mScrollList->firstVisibleIndex();
  const int last  = mScrollList->lastVisibleIndex();
  if (first < 0 || last < 0) return;

  for (int i = first; i <= last && i < static_cast<int>(mRows.size()); ++i) {
    const float top = mListRect.T + (i - first) * kRowH;
    const IRECT rr(mListRect.L, top, mListRect.R, top + kRowH);
    if (rr.Contains(x, y)) {
      const auto& row = mRows[i];
      if (mod.R) {
        // Right-click → popup menu.
        IGraphics* g = GetUI();
        if (g) {
          mCtxModelId = row.id;
          IPopupMenu menu;
          menu.AddItem("Rename", kCmdRename);
          g->CreatePopupMenu(*this, menu, x, y);
        }
        return;
      }
      // Left-click → load into active slot.
      if (mOnModelClicked) {
        mOnModelClicked(row.t3k_tone_id, row.t3k_model_id);
      }
      return;
    }
  }
}

void LibraryView::OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int /*valIdx*/)
{
  if (!pSelectedMenu) return;
  const int idx = pSelectedMenu->GetChosenItemIdx();
  if (idx < 0) return;

  const auto* item = pSelectedMenu->GetItem(idx);
  if (!item) return;
  switch (item->GetTag()) {
    case kCmdRename:
      if (mCtxModelId > 0) {
        showRenameOverlay(mCtxModelId);
      }
      break;
    default: break;
  }
  mCtxModelId = 0;
}

void LibraryView::refresh()
{
  mRows = ::t3k::library::LibraryDb::instance().queryByName(mSearch);
  if (mScrollList) mScrollList->invalidateHeights();
  SetDirty(false);
}

void LibraryView::hideRenameOverlay()
{
  if (mRenameOverlay) {
    mRenameOverlay->Hide(true);
  }
  mRenameTargetId = 0;
}

void LibraryView::showRenameOverlay(int64_t modelId)
{
  mRenameTargetId = modelId;
  if (!mRenameOverlay || !mScrollList) return;

  const int rowIdx = findRowIndexById(modelId);
  if (rowIdx < 0) return;

  // Anchor the overlay under the current visible row rect. The hit-test
  // math in OnMouseDown already approximates the visible row top, so we
  // reuse the same approximation here.
  const int first = mScrollList->firstVisibleIndex();
  if (first < 0 || rowIdx < first) return;
  const float top = mListRect.T + (rowIdx - first) * kRowH;
  const IRECT rowRect(mListRect.L, top, mListRect.R, top + kRowH);

  const auto& row = mRows[rowIdx];
  mRenameOverlay->show(rowRect, row.effectiveDisplayName());
}

int LibraryView::findRowIndexById(int64_t modelId) const
{
  for (size_t i = 0; i < mRows.size(); ++i) {
    if (mRows[i].id == modelId) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace t3k::ui
