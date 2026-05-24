// LibraryView.cpp — Phase 10 Library tab body. Replaces the Phase 3
// flat-row implementation.
//
// See LibraryView.h for architecture notes. Key implementation choices:
//   - We virtualize row drawing via T3kVScrollList's draw callback so
//     a 1000-row library doesn't construct 1000 IControl children.
//   - The scroll list's draw callback paints into a pre-computed
//     itemRect; rows are 88px tall and carry a thumbnail tile +
//     name/meta stack.
//   - Selection is row-id based (mSelectedId). Clicking a row sets
//     mSelectedId, repaints the strip with a 2px left-edge accent on
//     the selected row, and reveals the detail pane's action buttons.
//   - The Phase 4 TEST NET button has been retired — Phase 4 is well
//     past smoke-test stage and the button no longer earns its slot.
//   - Removal asks the user for confirmation via a second popup menu;
//     no separate modal control is introduced (keeps Block C scope
//     tight). The flow is:
//        1) Right-click row → popup with "Remove from library…"
//        2) Choose Remove → second popup "Confirm delete? Yes / No"
//        3) Yes → LibraryDb::removeRow + Paths::deleteModelFiles
//   - The detail-pane Remove button uses the same two-step flow.

#include "LibraryView.h"

#include <algorithm>
#include <cctype>
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
#include "../../library/Paths.h"
#include "../../config.h"  // ICON_*_FN

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// Header row dimensions.
constexpr float kHeaderH       = 56.f;
constexpr float kHeaderPad     = 16.f;
constexpr float kRescanBtnW    = 96.f;
constexpr float kRescanBtnH    = 28.f;
constexpr float kSearchH       = 32.f;

// Body split.
constexpr float kDetailW       = 300.f;     // right-side detail pane width
constexpr float kBodyGutter    = 12.f;      // gap between list and detail

// Row metrics.
constexpr float kRowH          = 88.f;
constexpr float kRowPadL       = 12.f;
constexpr float kRowPadR       = 16.f;
constexpr float kThumbSize     = 64.f;      // square gear-icon tile

// Detail-pane metrics.
constexpr float kDetailPad     = 16.f;
constexpr float kHeroSize      = 128.f;     // large gear-icon tile
constexpr float kDetailBtnH    = 32.f;
constexpr float kDetailBtnGap  = 8.f;

// Popup menu commands. Reserve a range so the two-step confirm flow
// doesn't collide with the first-stage menu.
enum PopupCmd {
  kCmdNone           = 0,
  kCmdRename         = 1,
  kCmdReveal         = 2,
  kCmdRemove         = 3,
  kCmdConfirmRemove  = 100,
  kCmdCancelRemove   = 101
};

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

// Map gear_type → resource filename. Falls back to the pedal icon for
// unknown types (mirrors T3kSlot's mapping).
const char* GearIconFor(const std::string& gearType)
{
  if (gearType == "amp")       return ICON_AMP_FN;
  if (gearType == "cab")       return ICON_CAB_FN;
  if (gearType == "outboard")  return ICON_OUTBOARD_FN;
  if (gearType == "full-rig")  return ICON_FULLRIG_FN;
  return ICON_PEDAL_FN;
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

void LibraryView::Hide(bool hide)
{
  IControl::Hide(hide);
  // iPlug2 attaches all controls flat — hiding this view does NOT
  // auto-propagate. Cascade to each child so the Library chrome doesn't
  // leak onto the Tone tab when the user switches away.
  if (mSearchBar)     mSearchBar    ->Hide(hide);
  if (mRescanBtn)     mRescanBtn    ->Hide(hide);
  if (mScrollList)    mScrollList   ->Hide(hide);
  if (mRenameOverlay) mRenameOverlay->Hide(hide);
  // Detail-pane buttons follow updateDetailButtons() rules — when
  // unhiding the view we honor selection state instead of unhiding
  // them unconditionally.
  if (hide) {
    if (mLoadBtn)   mLoadBtn  ->Hide(true);
    if (mRenameBtn) mRenameBtn->Hide(true);
    if (mRevealBtn) mRevealBtn->Hide(true);
    if (mRemoveBtn) mRemoveBtn->Hide(true);
  } else {
    updateDetailButtons();
  }
}

void LibraryView::OnResize()
{
  // Header row at the top, body fills the rest.
  auto split  = t3k::layout::rowFixedTop(mRECT, kHeaderH);
  mHeaderRect = split.first;
  mBodyRect   = split.second;

  // Header lays out left → right: search (flex) + Rescan.
  const float left  = mHeaderRect.L + kHeaderPad;
  const float right = mHeaderRect.R - kHeaderPad;
  const float midY  = mHeaderRect.MH();
  const float searchTop = midY - kSearchH * 0.5f;
  const float btnTop    = midY - kRescanBtnH * 0.5f;

  mRescanRect  = IRECT(right - kRescanBtnW, btnTop,
                       right,               btnTop + kRescanBtnH);
  mSearchRect  = IRECT(left,                searchTop,
                       mRescanRect.L - t3k::theme::kS3,
                       searchTop + kSearchH);

  // Body split: list on the left, detail on the right.
  mDetailRect = IRECT(mBodyRect.R - kDetailW, mBodyRect.T,
                      mBodyRect.R,            mBodyRect.B);
  mListRect   = IRECT(mBodyRect.L,            mBodyRect.T,
                      mDetailRect.L - kBodyGutter, mBodyRect.B);

  // Detail-pane child button rects (vertical stack at the bottom).
  const float btnL = mDetailRect.L + kDetailPad;
  const float btnR = mDetailRect.R - kDetailPad;
  const float bottomPad = kDetailPad;
  float by = mDetailRect.B - bottomPad - kDetailBtnH;

  mRemoveBtnRect = IRECT(btnL, by, btnR, by + kDetailBtnH);
  by -= kDetailBtnH + kDetailBtnGap;
  mRevealBtnRect = IRECT(btnL, by, btnR, by + kDetailBtnH);
  by -= kDetailBtnH + kDetailBtnGap;
  mRenameBtnRect = IRECT(btnL, by, btnR, by + kDetailBtnH);
  by -= kDetailBtnH + kDetailBtnGap;
  mLoadBtnRect   = IRECT(btnL, by, btnR, by + kDetailBtnH);

  if (mSearchBar)  mSearchBar ->SetTargetAndDrawRECTs(mSearchRect);
  if (mRescanBtn)  mRescanBtn ->SetTargetAndDrawRECTs(mRescanRect);
  if (mScrollList) mScrollList->SetTargetAndDrawRECTs(mListRect);
  if (mLoadBtn)    mLoadBtn   ->SetTargetAndDrawRECTs(mLoadBtnRect);
  if (mRenameBtn)  mRenameBtn ->SetTargetAndDrawRECTs(mRenameBtnRect);
  if (mRevealBtn)  mRevealBtn ->SetTargetAndDrawRECTs(mRevealBtnRect);
  if (mRemoveBtn)  mRemoveBtn ->SetTargetAndDrawRECTs(mRemoveBtnRect);
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
        const bool isSelected = (row.id == mSelectedId);

        // Row background — alternating tint, with an accent fill +
        // 2px left-edge bar when the row is selected.
        if (isSelected) {
          gg->FillRect(th::kBgElevated, r);
          gg->FillRect(th::kAccent,
                       IRECT(r.L, r.T, r.L + 2.f, r.B));
        } else if (i % 2 == 1) {
          gg->FillRect(th::kBgSurface, r);
        }

        // Thumbnail tile (square, gear-icon SVG centered).
        const float thumbTop = r.T + (kRowH - kThumbSize) * 0.5f;
        const IRECT thumbR(r.L + kRowPadL, thumbTop,
                           r.L + kRowPadL + kThumbSize,
                           thumbTop + kThumbSize);
        gg->FillRoundRect(th::kBgBase, thumbR, th::kRadiusMd);
        gg->DrawRoundRect(th::kBorder, thumbR, th::kRadiusMd, nullptr, 1.f);
        if (ISVG svg = gg->LoadSVG(GearIconFor(row.gear_type)); svg.IsValid()) {
          const float inset = 12.f;
          const IRECT iconR = thumbR.GetPadded(-inset);
          gg->DrawSVG(svg, iconR);
        }

        // Text column (right of the thumbnail).
        const float textL = thumbR.R + 14.f;
        const float textR = r.R - kRowPadR;

        // Display name (16px Inter Medium).
        const IRECT nameR(textL, r.T + 14.f,
                          textR, r.T + 14.f + 20.f);
        gg->DrawText(IText(th::kTypeH2, th::kText,
                           th::kFontBodyMed, EAlign::Near, EVAlign::Top),
                     row.effectiveDisplayName().c_str(), nameR);

        // Creator + gear_type, on the second line.
        const IRECT metaR(textL, nameR.B + 4.f,
                          textR, nameR.B + 4.f + 16.f);
        std::string meta;
        if (!row.t3k_creator.empty()) meta = row.t3k_creator;
        if (!row.gear_type.empty()) {
          if (!meta.empty()) meta += " \xC2\xB7 ";
          meta += row.gear_type;
        }
        if (meta.empty()) meta = "(unknown source)";
        gg->DrawText(IText(th::kTypeBody, th::kTextMuted,
                           th::kFontBody, EAlign::Near, EVAlign::Top),
                     meta.c_str(), metaR);

        // Size + variant name on the third line.
        const IRECT sizeR(textL, metaR.B + 2.f,
                          textR, metaR.B + 2.f + 14.f);
        std::string size = FormatSize(row.size_bytes);
        if (!row.model_name.empty() && row.model_name != row.display_name) {
          size += "  \xC2\xB7  " + row.model_name;
        }
        gg->DrawText(IText(th::kTypeSmall, th::kTextDim,
                           th::kFontBody, EAlign::Near, EVAlign::Top),
                     size.c_str(), sizeR);

        // Row separator.
        gg->FillRect(th::kBorder, IRECT(r.L, r.B - 1.f, r.R, r.B));
      });
  g->AttachControl(mScrollList);

  // Shared rename overlay — hidden until showRenameOverlay positions it
  // and Hide(false)s it.
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

  // Detail-pane action buttons. Attached hidden; updateDetailButtons()
  // reveals them when a row is selected.
  mLoadBtn = new T3kButton(mLoadBtnRect, "LOAD INTO CHAIN",
      [this]() { this->loadSelected(); },
      T3kButton::Variant::Primary);
  mRenameBtn = new T3kButton(mRenameBtnRect, "RENAME",
      [this]() { if (mSelectedId > 0) this->showRenameOverlay(mSelectedId); },
      T3kButton::Variant::Secondary);
  mRevealBtn = new T3kButton(mRevealBtnRect, "SHOW IN EXPLORER",
      [this]() { this->revealSelected(); },
      T3kButton::Variant::Secondary);
  mRemoveBtn = new T3kButton(mRemoveBtnRect, "REMOVE FROM LIBRARY",
      [this]() {
        // Two-step confirm via popup-menu (avoids a dedicated modal).
        if (mSelectedId <= 0) return;
        mRemoveConfirmId = mSelectedId;
        IGraphics* gg = this->GetUI();
        if (!gg) return;
        IPopupMenu menu;
        menu.AddItem("Yes, permanently delete files", kCmdConfirmRemove);
        menu.AddItem("Cancel", kCmdCancelRemove);
        const float anchorX = mRemoveBtnRect.MW();
        const float anchorY = mRemoveBtnRect.T;
        gg->CreatePopupMenu(*this, menu, anchorX, anchorY);
      },
      T3kButton::Variant::Secondary);
  g->AttachControl(mLoadBtn);
  g->AttachControl(mRenameBtn);
  g->AttachControl(mRevealBtn);
  g->AttachControl(mRemoveBtn);
  updateDetailButtons();

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

  // 1px vertical divider between the list and the detail pane.
  g.FillRect(th::kBorder,
             IRECT(mDetailRect.L - 1.f, mBodyRect.T,
                   mDetailRect.L,       mBodyRect.B));

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

  // ── Detail pane ────────────────────────────────────────────
  // Always draws; the right side of the body is dedicated to it.
  // When nothing is selected we draw a muted placeholder hint.
  if (mSelectedId <= 0) {
    g.DrawText(IText(th::kTypeBody, th::kTextDim,
                     th::kFontBody, EAlign::Center, EVAlign::Middle),
               "Select a model to see details.",
               mDetailRect);
    return;
  }

  // Find the selected row in the current results. If it was filtered
  // out by a search change, just bail.
  const ::t3k::library::ModelRow* row = nullptr;
  for (const auto& r : mRows) {
    if (r.id == mSelectedId) { row = &r; break; }
  }
  if (!row) return;

  // Hero gear-icon tile centered horizontally near the top of the
  // detail pane.
  const float heroL = mDetailRect.MW() - kHeroSize * 0.5f;
  const float heroT = mDetailRect.T + kDetailPad;
  const IRECT heroR(heroL, heroT, heroL + kHeroSize, heroT + kHeroSize);
  g.FillRoundRect(th::kBgSurface, heroR, th::kRadiusLg);
  g.DrawRoundRect(th::kBorder,    heroR, th::kRadiusLg, nullptr, 1.f);
  if (ISVG svg = g.LoadSVG(GearIconFor(row->gear_type)); svg.IsValid()) {
    g.DrawSVG(svg, heroR.GetPadded(-28.f));
  }

  // Stacked text below the hero. We carry a running y-cursor.
  float y = heroR.B + kDetailPad;
  const float lineL = mDetailRect.L + kDetailPad;
  const float lineR = mDetailRect.R - kDetailPad;

  // Name (Anton 20px — slightly smaller than h1 so longer names fit).
  const IRECT nameR(lineL, y, lineR, y + 26.f);
  g.DrawText(IText(20.f, th::kText, th::kFontDisplay,
                   EAlign::Center, EVAlign::Top),
             row->effectiveDisplayName().c_str(), nameR);
  y = nameR.B + 4.f;

  // Creator.
  if (!row->t3k_creator.empty()) {
    const IRECT creatorR(lineL, y, lineR, y + 16.f);
    g.DrawText(IText(th::kTypeBody, th::kTextMuted, th::kFontBody,
                     EAlign::Center, EVAlign::Top),
               row->t3k_creator.c_str(), creatorR);
    y = creatorR.B + 6.f;
  }

  // Meta line: gear_type · make · variant.
  std::string meta;
  if (!row->gear_type.empty()) meta = row->gear_type;
  if (!row->make.empty()) {
    if (!meta.empty()) meta += " \xC2\xB7 ";
    meta += row->make;
  }
  if (!row->model_name.empty() && row->model_name != row->display_name) {
    if (!meta.empty()) meta += " \xC2\xB7 ";
    meta += row->model_name;
  }
  if (!meta.empty()) {
    const IRECT metaR(lineL, y, lineR, y + 14.f);
    g.DrawText(IText(th::kTypeSmall, th::kTextDim, th::kFontBody,
                     EAlign::Center, EVAlign::Top),
               meta.c_str(), metaR);
    y = metaR.B + 8.f;
  }

  // Size + format (kind).
  {
    std::string sizeLine = FormatSize(row->size_bytes);
    if (!row->kind.empty()) {
      sizeLine += " \xC2\xB7 ";
      std::string k = row->kind;
      for (char& c : k) c = static_cast<char>(std::toupper((unsigned char)c));
      sizeLine += k;
    }
    const IRECT sizeR(lineL, y, lineR, y + 14.f);
    g.DrawText(IText(th::kTypeSmall, th::kTextDim, th::kFontBody,
                     EAlign::Center, EVAlign::Top),
               sizeLine.c_str(), sizeR);
    y = sizeR.B + 10.f;
  }

  // Description — wraps. NanoVG's DrawText doesn't auto-wrap multiline;
  // the description usually fits in 3 lines so we let it clip if it's
  // too long. Future polish: a proper word-wrap helper.
  if (!row->t3k_description.empty()) {
    const float descBottom = mLoadBtnRect.T - kDetailPad;
    if (descBottom > y + 12.f) {
      const IRECT descR(lineL, y, lineR, descBottom);
      g.DrawText(IText(th::kTypeSmall, th::kTextMuted, th::kFontBody,
                       EAlign::Center, EVAlign::Top),
                 row->t3k_description.c_str(), descR);
    }
  }
}

void LibraryView::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  if (!mListRect.Contains(x, y)) {
    // Clicks on the detail pane fall through to its child IControls.
    return;
  }
  if (!mScrollList) return;

  const int first = mScrollList->firstVisibleIndex();
  const int last  = mScrollList->lastVisibleIndex();
  if (first < 0 || last < 0) return;

  for (int i = first; i <= last && i < static_cast<int>(mRows.size()); ++i) {
    const float top = mListRect.T + (i - first) * kRowH;
    const IRECT rr(mListRect.L, top, mListRect.R, top + kRowH);
    if (rr.Contains(x, y)) {
      const auto& row = mRows[i];
      mSelectedId = row.id;
      updateDetailButtons();
      SetDirty(false);

      if (mod.R) {
        IGraphics* g = GetUI();
        if (g) {
          mCtxModelId = row.id;
          IPopupMenu menu;
          menu.AddItem("Rename\xE2\x80\xA6",            kCmdRename);
          menu.AddItem("Show in Explorer",              kCmdReveal);
          menu.AddSeparator();
          menu.AddItem("Remove from library\xE2\x80\xA6", kCmdRemove);
          g->CreatePopupMenu(*this, menu, x, y);
        }
        return;
      }
      // Left-click → just select. Loading into the chain is a
      // deliberate action via the LOAD INTO CHAIN button to avoid
      // accidentally swapping the active model on a random click.
      return;
    }
  }
}

void LibraryView::OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int /*valIdx*/)
{
  if (!pSelectedMenu) return;
  const int idx = pSelectedMenu->GetChosenItemIdx();
  if (idx < 0) {
    mCtxModelId      = 0;
    mRemoveConfirmId = 0;
    return;
  }

  const auto* item = pSelectedMenu->GetItem(idx);
  if (!item) {
    mCtxModelId      = 0;
    mRemoveConfirmId = 0;
    return;
  }

  switch (item->GetTag()) {
    case kCmdRename:
      if (mCtxModelId > 0) showRenameOverlay(mCtxModelId);
      break;
    case kCmdReveal:
      if (mCtxModelId > 0) {
        mSelectedId = mCtxModelId;
        updateDetailButtons();
        revealSelected();
      }
      break;
    case kCmdRemove:
      if (mCtxModelId > 0) {
        mRemoveConfirmId = mCtxModelId;
        IGraphics* g = GetUI();
        if (g) {
          IPopupMenu confirm;
          confirm.AddItem("Yes, permanently delete files", kCmdConfirmRemove);
          confirm.AddItem("Cancel", kCmdCancelRemove);
          // Drop the confirm popup at the row's center (iPlug doesn't
          // surface the original click coords once we're inside this
          // handler).
          const int rowIdx = findRowIndexById(mCtxModelId);
          float ax = mListRect.MW();
          float ay = mListRect.MH();
          if (rowIdx >= 0 && mScrollList) {
            const int first = mScrollList->firstVisibleIndex();
            if (rowIdx >= first) {
              ay = mListRect.T + (rowIdx - first) * kRowH + kRowH * 0.5f;
            }
          }
          g->CreatePopupMenu(*this, confirm, ax, ay);
        }
      }
      break;
    case kCmdConfirmRemove:
      if (mRemoveConfirmId > 0) {
        mSelectedId = mRemoveConfirmId;
        removeSelected();
      }
      mRemoveConfirmId = 0;
      break;
    case kCmdCancelRemove:
      mRemoveConfirmId = 0;
      break;
    default: break;
  }
  mCtxModelId = 0;
}

void LibraryView::refresh()
{
  mRows = ::t3k::library::LibraryDb::instance().queryByName(mSearch);
  if (mSelectedId > 0) {
    bool stillVisible = false;
    for (const auto& r : mRows) {
      if (r.id == mSelectedId) { stillVisible = true; break; }
    }
    if (!stillVisible) {
      mSelectedId = 0;
      updateDetailButtons();
    }
  }
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

void LibraryView::removeSelected()
{
  if (mSelectedId <= 0) return;
  auto row = ::t3k::library::LibraryDb::instance().findById(mSelectedId);
  if (!row.has_value()) {
    mSelectedId = 0;
    updateDetailButtons();
    SetDirty(false);
    return;
  }
  // Compute the sidecar path: ModelSidecar writes "<stem>.tone3000.json"
  // next to the .nam file, so strip the .nam extension and append.
  std::string sidecar;
  if (!row->uri.empty()) {
    const auto dot = row->uri.find_last_of('.');
    if (dot != std::string::npos) {
      sidecar = row->uri.substr(0, dot) + ".tone3000.json";
    }
  }
  const std::string image = row->t3k_image_path.value_or(std::string{});

  ::t3k::library::Paths::deleteModelFiles(row->uri, sidecar, image);
  ::t3k::library::LibraryDb::instance().removeRow(mSelectedId);

  mSelectedId = 0;
  updateDetailButtons();
  refresh();
}

void LibraryView::revealSelected()
{
  if (mSelectedId <= 0) return;
  auto row = ::t3k::library::LibraryDb::instance().findById(mSelectedId);
  if (!row.has_value() || row->uri.empty()) return;
  ::t3k::library::Paths::revealInExplorer(row->uri);
}

void LibraryView::loadSelected()
{
  if (mSelectedId <= 0 || !mOnModelClicked) return;
  auto row = ::t3k::library::LibraryDb::instance().findById(mSelectedId);
  if (!row.has_value()) return;
  mOnModelClicked(row->t3k_tone_id, row->t3k_model_id);
}

void LibraryView::updateDetailButtons()
{
  const bool visible = !IsHidden() && mSelectedId > 0;
  if (mLoadBtn)   mLoadBtn  ->Hide(!visible);
  if (mRenameBtn) mRenameBtn->Hide(!visible);
  if (mRevealBtn) mRevealBtn->Hide(!visible);
  if (mRemoveBtn) mRemoveBtn->Hide(!visible);
}

}  // namespace t3k::ui
