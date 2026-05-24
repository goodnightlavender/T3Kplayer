// LibraryView.cpp — see LibraryView.h.
//
// Implementation notes:
//   - The five sidebar accordions mirror CloudView's set so the two tabs
//     feel like the same product. Tags is still a placeholder until
//     LibraryDb carries tag rows; the other four (Gear, Makes, Creators,
//     Technical/Format) drive live re-querying.
//   - The grid uses kCols columns. The card pool grows lazily — we hold
//     onto T3kLibraryCard children even when the user scrolls or
//     filters, and Hide(true) extras so iPlug2 skips their paint.
//   - Removal still uses the two-step popup-menu confirm.

#include "LibraryView.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_set>

#include "IGraphics.h"

#include "../theme.h"
#include "../layout.h"
#include "../controls/T3kAccordion.h"
#include "../controls/T3kButton.h"
#include "../controls/T3kLibraryCard.h"
#include "../controls/T3kRenameOverlay.h"
#include "../controls/T3kSearchBar.h"
#include "T3kDetailModal.h"
#include "../../library/EventBus.h"
#include "../../library/LibraryScanner.h"
#include "../../library/Paths.h"
#include "../../config.h"  // ICON_*_FN

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// Layout constants tuned for the 1280x800 default window.
constexpr float kHeaderH        = 56.f;
constexpr float kHeaderPad      = 16.f;
constexpr float kSearchH        = 32.f;
constexpr float kRescanBtnW     = 96.f;
constexpr float kRescanBtnH     = 28.f;

constexpr float kSidebarW       = 220.f;
constexpr float kSidebarPad     = 12.f;
constexpr float kCheckboxRowH   = 26.f;
constexpr float kCheckboxBoxSz  = 14.f;

constexpr float kDetailH        = 88.f;
constexpr float kDetailPad      = 14.f;
constexpr float kDetailBtnW     = 140.f;
constexpr float kDetailBtnH     = 30.f;
constexpr float kDetailBtnGap   = 8.f;

constexpr float kGridPad        = 16.f;
constexpr float kCardW          = 144.f;
constexpr float kCardH          = 168.f;
constexpr float kCardGap        = 12.f;
constexpr int   kCols           = 6;

enum PopupCmd {
  kCmdNone           = 0,
  kCmdRename         = 1,
  kCmdReveal         = 2,
  kCmdRemove         = 3,
  kCmdConfirmRemove  = 100,
  kCmdCancelRemove   = 101,
};

// Format size_bytes as a human-readable string.
std::string FormatSize(int64_t bytes)
{
  constexpr int64_t kMb = 1024 * 1024;
  constexpr int64_t kKb = 1024;
  char buf[64];
  if (bytes >= kMb) {
    std::snprintf(buf, sizeof(buf), "%.1f MB",
                  static_cast<double>(bytes) / static_cast<double>(kMb));
  } else if (bytes >= kKb) {
    std::snprintf(buf, sizeof(buf), "%lld KB",
                  static_cast<long long>(bytes / kKb));
  } else {
    std::snprintf(buf, sizeof(buf), "%lld B",
                  static_cast<long long>(bytes));
  }
  return buf;
}

const char* GearLabelFor(const std::string& g)
{
  if (g == "amp")       return "Amp";
  if (g == "cab")       return "Cab";
  if (g == "outboard")  return "Outboard";
  if (g == "full-rig")  return "Full rig";
  if (g == "pedal")     return "Pedal";
  return g.c_str();
}

const char* GearIconResource(const std::string& g)
{
  if (g == "amp")       return ICON_AMP_FN;
  if (g == "cab")       return ICON_CAB_FN;
  if (g == "outboard")  return ICON_OUTBOARD_FN;
  if (g == "full-rig")  return ICON_FULLRIG_FN;
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
  if (mSearchBar)     mSearchBar    ->Hide(hide);
  if (mRescanBtn)     mRescanBtn    ->Hide(hide);
  if (mGearAcc)       mGearAcc      ->Hide(hide);
  if (mTagsAcc)       mTagsAcc      ->Hide(hide);
  if (mMakesAcc)      mMakesAcc     ->Hide(hide);
  if (mCreatorsAcc)   mCreatorsAcc  ->Hide(hide);
  if (mTechAcc)       mTechAcc      ->Hide(hide);
  if (mRenameOverlay) mRenameOverlay->Hide(true);  // always hidden between sessions
  if (mDetailModal)   mDetailModal  ->Hide(true);  // ditto
  if (hide) {
    if (mLoadBtn)   mLoadBtn  ->Hide(true);
    if (mRenameBtn) mRenameBtn->Hide(true);
    if (mRevealBtn) mRevealBtn->Hide(true);
    if (mRemoveBtn) mRemoveBtn->Hide(true);
    for (auto* c : mCards) if (c) c->Hide(true);
  } else {
    updateDetailButtons();
    layoutCards();
  }
}

void LibraryView::OnResize()
{
  namespace th = ::t3k::theme;

  // Header (search + rescan) at the top.
  auto hsplit = t3k::layout::rowFixedTop(mRECT, kHeaderH);
  mHeaderRect = hsplit.first;
  IRECT below = hsplit.second;

  const float left  = mHeaderRect.L + kHeaderPad;
  const float right = mHeaderRect.R - kHeaderPad;
  const float midY  = mHeaderRect.MH();
  const float searchTop = midY - kSearchH * 0.5f;
  const float btnTop    = midY - kRescanBtnH * 0.5f;

  mSortRect   = IRECT(right - kRescanBtnW, btnTop,
                      right,               btnTop + kRescanBtnH);
  mSearchRect = IRECT(left, searchTop,
                      mSortRect.L - th::kS3,
                      searchTop + kSearchH);

  // Body: sidebar (left) + detail strip (bottom) + grid (remaining).
  mSidebarRect = IRECT(below.L, below.T, below.L + kSidebarW, below.B);
  const IRECT body(mSidebarRect.R, below.T, below.R, below.B);
  mDetailRect = IRECT(body.L, body.B - kDetailH, body.R, body.B);
  mGridRect   = IRECT(body.L, body.T,            body.R, mDetailRect.T);

  if (mSearchBar) mSearchBar->SetTargetAndDrawRECTs(mSearchRect);
  if (mRescanBtn) mRescanBtn->SetTargetAndDrawRECTs(mSortRect);

  layoutSidebar();

  // Detail-strip buttons — right-aligned row.
  const float by = mDetailRect.MH() - kDetailBtnH * 0.5f;
  float bx = mDetailRect.R - kDetailPad - kDetailBtnW;
  mRemoveBtnRect = IRECT(bx, by, bx + kDetailBtnW, by + kDetailBtnH);
  bx -= kDetailBtnW + kDetailBtnGap;
  mRevealBtnRect = IRECT(bx, by, bx + kDetailBtnW, by + kDetailBtnH);
  bx -= kDetailBtnW + kDetailBtnGap;
  mRenameBtnRect = IRECT(bx, by, bx + kDetailBtnW, by + kDetailBtnH);
  bx -= kDetailBtnW + kDetailBtnGap;
  mLoadBtnRect   = IRECT(bx, by, bx + kDetailBtnW, by + kDetailBtnH);

  if (mLoadBtn)   mLoadBtn  ->SetTargetAndDrawRECTs(mLoadBtnRect);
  if (mRenameBtn) mRenameBtn->SetTargetAndDrawRECTs(mRenameBtnRect);
  if (mRevealBtn) mRevealBtn->SetTargetAndDrawRECTs(mRevealBtnRect);
  if (mRemoveBtn) mRemoveBtn->SetTargetAndDrawRECTs(mRemoveBtnRect);

  layoutCards();
}

void LibraryView::layoutSidebar()
{
  namespace th = ::t3k::theme;
  T3kAccordion* accs[] = {
      mGearAcc, mTagsAcc, mMakesAcc, mCreatorsAcc, mTechAcc,
  };
  // Per-accordion expanded content height.
  const float contentH[] = {
      5 * kCheckboxRowH + 8.f,                                    // Gear
      kCheckboxRowH    + 8.f,                                     // Tags
      std::max<float>(kCheckboxRowH, mAllMakes.size()    * kCheckboxRowH) + 8.f,
      std::max<float>(kCheckboxRowH, mAllCreators.size() * kCheckboxRowH) + 8.f,
      2 * kCheckboxRowH + 8.f,                                    // Technical (Format)
  };
  float y = mSidebarRect.T + kSidebarPad;
  const float left  = mSidebarRect.L + kSidebarPad;
  const float right = mSidebarRect.R - kSidebarPad;
  for (size_t i = 0; i < 5; ++i) {
    T3kAccordion* a = accs[i];
    if (!a) continue;
    const float h = T3kAccordion::headerHeight()
                  + (a->isOpen() ? contentH[i] : 0.f);
    a->SetTargetAndDrawRECTs(IRECT(left, y, right, y + h));
    y += h + th::kS2;
  }
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

  mRescanBtn = new T3kButton(mSortRect, "RESCAN",
      [this]() { ::t3k::library::LibraryScanner::instance().rescan(); },
      T3kButton::Variant::Secondary);
  g->AttachControl(mRescanBtn);

  auto wireToggle = [this](T3kAccordion* acc) {
    acc->setOnToggle([this](bool /*isOpen*/) {
      this->layoutSidebar();
      this->SetDirty(false);
    });
  };

  const IRECT placeholder(0.f, 0.f, 1.f, 1.f);

  mGearAcc = new T3kAccordion(placeholder,
      "Gear",
      []() { return 5 * kCheckboxRowH + 8.f; },
      [this](const IRECT& r) { this->drawGearAccordion(r); },
      /*initiallyOpen*/ true);
  wireToggle(mGearAcc);
  g->AttachControl(mGearAcc);

  // Tags / Makes and Models / Creators accordions — hidden per the
  // 2026-05-25 polish round. Local library doesn't carry tag data yet
  // and the make/creator surfaces were noisy with sparse libraries.
  // Flip the toggle to re-enable.
  constexpr bool kShowExtraFilters = false;
  if constexpr (kShowExtraFilters) {
    mTagsAcc = new T3kAccordion(placeholder,
        "Tags",
        []() { return kCheckboxRowH + 8.f; },
        [this](const IRECT& r) { this->drawTagsAccordion(r); },
        /*initiallyOpen*/ false);
    wireToggle(mTagsAcc);
    g->AttachControl(mTagsAcc);

    mMakesAcc = new T3kAccordion(placeholder,
        "Makes and Models",
        [this]() {
          return std::max<float>(kCheckboxRowH,
                                 mAllMakes.size() * kCheckboxRowH) + 8.f;
        },
        [this](const IRECT& r) { this->drawMakesAccordion(r); },
        /*initiallyOpen*/ false);
    wireToggle(mMakesAcc);
    g->AttachControl(mMakesAcc);

    mCreatorsAcc = new T3kAccordion(placeholder,
        "Creators",
        [this]() {
          return std::max<float>(kCheckboxRowH,
                                 mAllCreators.size() * kCheckboxRowH) + 8.f;
        },
        [this](const IRECT& r) { this->drawCreatorsAccordion(r); },
        /*initiallyOpen*/ false);
    wireToggle(mCreatorsAcc);
    g->AttachControl(mCreatorsAcc);
  }

  mTechAcc = new T3kAccordion(placeholder,
      "Technical",
      []() { return 2 * kCheckboxRowH + 8.f; },
      [this](const IRECT& r) { this->drawTechnicalAccordion(r); },
      /*initiallyOpen*/ false);
  wireToggle(mTechAcc);
  g->AttachControl(mTechAcc);

  // Rename overlay — full-window placeholder, repositioned by show().
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

  // Detail-strip buttons — created hidden; updateDetailButtons() reveals
  // them when a card is selected.
  mLoadBtn = new T3kButton(mLoadBtnRect, "LOAD INTO CHAIN",
      [this]() { this->loadSelected(); },
      T3kButton::Variant::Primary);
  mRenameBtn = new T3kButton(mRenameBtnRect, "RENAME",
      [this]() { this->renameSelected(); },
      T3kButton::Variant::Secondary);
  mRevealBtn = new T3kButton(mRevealBtnRect, "SHOW IN EXPLORER",
      [this]() { this->revealSelected(); },
      T3kButton::Variant::Secondary);
  mRemoveBtn = new T3kButton(mRemoveBtnRect, "REMOVE",
      [this]() {
        if (mSelectedId <= 0) return;
        mRemoveConfirmId = mSelectedId;
        IGraphics* gg = this->GetUI();
        if (!gg) return;
        IPopupMenu menu;
        menu.AddItem("Yes, permanently delete files", kCmdConfirmRemove);
        menu.AddItem("Cancel", kCmdCancelRemove);
        gg->CreatePopupMenu(*this, menu,
                            mRemoveBtnRect.MW(), mRemoveBtnRect.T);
      },
      T3kButton::Variant::Secondary);
  g->AttachControl(mLoadBtn);
  g->AttachControl(mRenameBtn);
  g->AttachControl(mRevealBtn);
  g->AttachControl(mRemoveBtn);
  updateDetailButtons();

  // Detail modal — attached last so it stays at the top of the z-order.
  // Sized to the full window via the view's parent rect (mRECT covers
  // the tab body, not the full window — but the modal is attached at
  // tab-body bounds and ToneRoot's own modals draw above it; that's
  // fine because we only show one of these at a time).
  mDetailModal = new T3kDetailModal(mRECT,
      [this]() {
        if (mDetailModal) mDetailModal->Hide(true);
      });
  g->AttachControl(mDetailModal);
  mDetailModal->Hide(true);

  // Subscribe to scanner events.
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

void LibraryView::refresh()
{
  mAllRows = ::t3k::library::LibraryDb::instance().queryByName(mSearch);
  recomputeFilterOptions();
  applyFilters();
}

void LibraryView::recomputeFilterOptions()
{
  std::unordered_set<std::string> makes, creators;
  for (const auto& r : mAllRows) {
    if (!r.make.empty())        makes.insert(r.make);
    if (!r.t3k_creator.empty()) creators.insert(r.t3k_creator);
  }
  mAllMakes.assign(makes.begin(), makes.end());
  mAllCreators.assign(creators.begin(), creators.end());
  std::sort(mAllMakes.begin(),    mAllMakes.end());
  std::sort(mAllCreators.begin(), mAllCreators.end());
  // Prune dead selections so toggling them off still works.
  for (auto it = mSelectedMakes.begin(); it != mSelectedMakes.end();) {
    if (makes.find(*it) == makes.end()) it = mSelectedMakes.erase(it);
    else ++it;
  }
  for (auto it = mSelectedCreators.begin(); it != mSelectedCreators.end();) {
    if (creators.find(*it) == creators.end()) it = mSelectedCreators.erase(it);
    else ++it;
  }
  layoutSidebar();
}

void LibraryView::applyFilters()
{
  mRows.clear();
  mVariantsByToneId.clear();
  // Walk mAllRows and group by t3k_tone_id. The first variant
  // matching a tone_id becomes the displayed "primary" row; subsequent
  // variants stack under it in mVariantsByToneId. Rows without a
  // tone_id (rare — local-only .nam imports without sidecar metadata)
  // are treated as standalone groups keyed by their numeric id.
  std::unordered_map<std::string, size_t> primaryIndexByTone;
  for (const auto& r : mAllRows) {
    if (!mSelectedGears.empty() &&
        mSelectedGears.find(r.gear_type) == mSelectedGears.end()) {
      continue;
    }
    if (!mSelectedMakes.empty() &&
        mSelectedMakes.find(r.make) == mSelectedMakes.end()) {
      continue;
    }
    if (!mSelectedCreators.empty() &&
        mSelectedCreators.find(r.t3k_creator) == mSelectedCreators.end()) {
      continue;
    }
    if (!mSelectedFormats.empty() &&
        mSelectedFormats.find(r.kind) == mSelectedFormats.end()) {
      continue;
    }
    const std::string key = r.t3k_tone_id.empty()
                              ? ("__local_" + std::to_string(r.id))
                              : r.t3k_tone_id;
    auto it = primaryIndexByTone.find(key);
    if (it == primaryIndexByTone.end()) {
      primaryIndexByTone.emplace(key, mRows.size());
      mRows.push_back(r);
      mVariantsByToneId[key].push_back(r);
    } else {
      mVariantsByToneId[key].push_back(r);
    }
  }
  // Drop selection if it filtered out.
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
  mScrollOffset = 0.f;
  ensureCardCount(static_cast<int>(mRows.size()));
  layoutCards();
  SetDirty(false);
}

void LibraryView::ensureCardCount(int n)
{
  IGraphics* g = GetUI();
  if (!g) return;
  while (static_cast<int>(mCards.size()) < n) {
    auto* card = new T3kLibraryCard(
        IRECT(0.f, 0.f, 1.f, 1.f),
        T3kLibraryCard::CardData{},
        [this](int64_t id) { this->onCardClicked(id); },
        [this](int64_t id, float x, float y) { this->onCardRightClicked(id, x, y); });
    card->setOnWheel([this](float d) { this->scrollBy(d); });
    card->setOnDblClick([this](int64_t id) { this->onCardDblClicked(id); });
    g->AttachControl(card);
    mCards.push_back(card);
  }
}

float LibraryView::gridContentHeight() const
{
  const int n = static_cast<int>(mRows.size());
  if (n == 0) return 0.f;
  const int rows = (n + kCols - 1) / kCols;
  return rows * kCardH + (rows - 1) * kCardGap + 2.f * kGridPad;
}

float LibraryView::gridViewportHeight() const
{
  return std::max(0.f, mGridRect.H());
}

void LibraryView::layoutCards()
{
  const int n = static_cast<int>(mRows.size());
  const float gridL = mGridRect.L + kGridPad;
  const float gridT = mGridRect.T + kGridPad - mScrollOffset;

  // Compute step sizes so the grid fills the available width even if
  // the body is wider than kCols * kCardW + gaps. We pin card width
  // and grow the gap until the row fits.
  const float availW = std::max<float>(0.f, mGridRect.W() - 2.f * kGridPad);
  const float totalCardsW = kCols * kCardW;
  const float gapX = (kCols > 1)
                       ? std::max(kCardGap, (availW - totalCardsW) / (kCols - 1))
                       : 0.f;

  for (int i = 0; i < static_cast<int>(mCards.size()); ++i) {
    T3kLibraryCard* card = mCards[i];
    if (!card) continue;
    if (i >= n) {
      card->Hide(true);
      continue;
    }
    const int row = i / kCols;
    const int col = i % kCols;
    const float x = gridL + col * (kCardW + gapX);
    const float y = gridT + row * (kCardH + kCardGap);
    const IRECT cardR(x, y, x + kCardW, y + kCardH);

    // Push the new data into the card.
    T3kLibraryCard::CardData d;
    d.id          = mRows[i].id;
    d.displayName = mRows[i].effectiveDisplayName();
    d.creator     = mRows[i].t3k_creator;
    d.gearType    = mRows[i].gear_type;
    std::string fmt = mRows[i].kind;
    for (char& c : fmt) c = static_cast<char>(std::toupper((unsigned char)c));
    d.format = std::move(fmt);
    // Image — prefer the sidecar's t3k_image_path (already cached to
    // disk by Downloader / ModelSidecar). Empty path -> card falls
    // back to the gear icon.
    if (mRows[i].t3k_image_path.has_value()) {
      d.imagePath = *mRows[i].t3k_image_path;
    }
    card->setData(std::move(d));
    card->setSelected(mRows[i].id == mSelectedId);

    // Hide cards whose Y rect lies entirely outside the body — iPlug2
    // doesn't auto-skip; without this, off-screen cards still paint
    // their fills/borders and break visual layering on tab switches.
    const bool visible = !IsHidden() &&
                         cardR.B > mGridRect.T &&
                         cardR.T < mGridRect.B;
    card->Hide(!visible);
    card->SetTargetAndDrawRECTs(cardR);
  }
  SetDirty(false);
}

void LibraryView::onCardClicked(int64_t id)
{
  mSelectedId = id;
  updateDetailButtons();
  for (auto* c : mCards) {
    if (c) c->setSelected(c->id() == id);
  }
  SetDirty(false);
}

void LibraryView::onCardDblClicked(int64_t id)
{
  showDetailFor(id);
}

void LibraryView::showDetailFor(int64_t id)
{
  if (!mDetailModal || id <= 0) return;
  // Find the row in mRows + look up its variants.
  const ::t3k::library::ModelRow* row = nullptr;
  for (const auto& r : mRows) {
    if (r.id == id) { row = &r; break; }
  }
  if (!row) return;
  const std::string toneKey = row->t3k_tone_id.empty()
                                ? ("__local_" + std::to_string(row->id))
                                : row->t3k_tone_id;
  T3kDetailModal::DetailData d;
  d.title       = row->effectiveDisplayName();
  d.creator     = row->t3k_creator;
  d.description = row->t3k_description;
  if (row->t3k_image_path.has_value()) d.imagePath = *row->t3k_image_path;
  // Subtitle: gear-type . variant count (single variant -> just gear).
  auto vit = mVariantsByToneId.find(toneKey);
  std::string sub;
  if (!row->gear_type.empty()) {
    sub = row->gear_type;
    // Title-case the first letter.
    if (!sub.empty()) sub[0] = static_cast<char>(std::toupper((unsigned char)sub[0]));
  }
  if (vit != mVariantsByToneId.end() && vit->second.size() > 1) {
    if (!sub.empty()) sub += " \xC2\xB7 ";
    sub += std::to_string(vit->second.size()) + " variants";
  }
  d.subtitle = std::move(sub);
  // Variants list (Makes-and-Models section in the reference).
  if (vit != mVariantsByToneId.end()) {
    for (const auto& v : vit->second) {
      std::string entry = v.model_name.empty()
                            ? v.effectiveDisplayName()
                            : v.model_name;
      if (!v.kind.empty()) {
        std::string k = v.kind;
        for (char& c : k) c = static_cast<char>(std::toupper((unsigned char)c));
        entry += "  \xC2\xB7  " + k;
      }
      d.makesModels.push_back(std::move(entry));
    }
  }
  // Tags placeholder — local sidecars don't carry tags yet. When the
  // sidecar schema gains them we'll plumb them through here.

  // Library actions: Load / Rename / Show in Explorer / Remove. We
  // capture the id rather than `this->mSelectedId` because the modal
  // can outlive the current selection.
  std::vector<T3kDetailModal::Action> actions = {
    { "LOAD INTO CHAIN", /*primary*/ true, [this, id]() {
        mSelectedId = id;
        if (mDetailModal) mDetailModal->Hide(true);
        loadSelected();
      } },
    { "RENAME",          false, [this, id]() {
        if (mDetailModal) mDetailModal->Hide(true);
        showRenameOverlay(id);
      } },
    { "SHOW IN EXPLORER", false, [this, id]() {
        mSelectedId = id;
        if (mDetailModal) mDetailModal->Hide(true);
        revealSelected();
      } },
    { "REMOVE",           false, [this, id]() {
        mSelectedId = id;
        if (mDetailModal) mDetailModal->Hide(true);
        // Two-step confirm — re-use the same popup-menu flow.
        IGraphics* gg = GetUI();
        if (!gg) return;
        mRemoveConfirmId = id;
        IPopupMenu menu;
        menu.AddItem("Yes, permanently delete files", kCmdConfirmRemove);
        menu.AddItem("Cancel", kCmdCancelRemove);
        gg->CreatePopupMenu(*this, menu, mGridRect.MW(), mGridRect.MH());
      } },
  };
  mDetailModal->show(std::move(d), std::move(actions));
}

void LibraryView::onCardRightClicked(int64_t id, float x, float y)
{
  // Select on right-click too so the popup operates on a clearly
  // highlighted card.
  onCardClicked(id);
  IGraphics* g = GetUI();
  if (!g) return;
  mCtxModelId = id;
  IPopupMenu menu;
  menu.AddItem("Rename\xE2\x80\xA6",              kCmdRename);
  menu.AddItem("Show in Explorer",                kCmdReveal);
  menu.AddSeparator();
  menu.AddItem("Remove from library\xE2\x80\xA6", kCmdRemove);
  g->CreatePopupMenu(*this, menu, x, y);
}

void LibraryView::OnMouseDown(float x, float y, const IMouseMod& /*mod*/)
{
  // Sidebar checkbox hit-test first; otherwise the click is a no-op at
  // this level (cards handle their own mouse events; detail-strip
  // buttons are children).
  handleSidebarClick(x, y);
}

void LibraryView::OnMouseWheel(float x, float y,
                               const IMouseMod& /*mod*/, float d)
{
  if (mGridRect.Contains(x, y)) scrollBy(d);
}

void LibraryView::scrollBy(float d)
{
  // 40px per wheel notch — matches T3kVScrollList's feel.
  const float step = 40.f * d;
  const float maxOffset =
      std::max(0.f, gridContentHeight() - gridViewportHeight());
  mScrollOffset = std::clamp(mScrollOffset - step, 0.f, maxOffset);
  layoutCards();
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
          g->CreatePopupMenu(*this, confirm, mGridRect.MW(), mGridRect.MH());
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

void LibraryView::renameSelected()
{
  if (mSelectedId > 0) showRenameOverlay(mSelectedId);
}

void LibraryView::updateDetailButtons()
{
  const bool visible = !IsHidden() && mSelectedId > 0;
  if (mLoadBtn)   mLoadBtn  ->Hide(!visible);
  if (mRenameBtn) mRenameBtn->Hide(!visible);
  if (mRevealBtn) mRevealBtn->Hide(!visible);
  if (mRemoveBtn) mRemoveBtn->Hide(!visible);
}

void LibraryView::showRenameOverlay(int64_t modelId)
{
  mRenameTargetId = modelId;
  if (!mRenameOverlay) return;
  auto row = ::t3k::library::LibraryDb::instance().findById(modelId);
  if (!row.has_value()) return;
  // Anchor under the detail-strip name area for predictability — any
  // row could be off-screen at the time the user picks Rename from
  // the popup, so we don't try to chase the card's position.
  const IRECT anchor(mDetailRect.L + kDetailPad,
                     mDetailRect.T + 8.f,
                     mDetailRect.L + kDetailPad + 320.f,
                     mDetailRect.T + 8.f + 36.f);
  mRenameOverlay->show(anchor, row->effectiveDisplayName());
}

bool LibraryView::handleSidebarClick(float x, float y)
{
  auto handle = [&](std::vector<std::pair<std::string, IRECT>>& rows,
                    std::unordered_set<std::string>& sel) -> bool {
    for (const auto& [v, rr] : rows) {
      if (rr.Contains(x, y)) {
        if (sel.count(v)) sel.erase(v);
        else              sel.insert(v);
        applyFilters();
        return true;
      }
    }
    return false;
  };
  if (handle(mGearRowRects,    mSelectedGears))    return true;
  if (handle(mMakeRowRects,    mSelectedMakes))    return true;
  if (handle(mCreatorRowRects, mSelectedCreators)) return true;
  if (handle(mFormatRowRects,  mSelectedFormats))  return true;
  return false;
}

void LibraryView::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Body background + 1px dividers.
  g.FillRect(th::kBgBase, mRECT);
  g.FillRect(th::kBorder,
             IRECT(mRECT.L, mHeaderRect.B - 1.f, mRECT.R, mHeaderRect.B));
  g.FillRect(th::kBorder,
             IRECT(mSidebarRect.R - 1.f, mSidebarRect.T,
                   mSidebarRect.R,       mSidebarRect.B));
  g.FillRect(th::kBorder,
             IRECT(mDetailRect.L, mDetailRect.T - 1.f,
                   mDetailRect.R, mDetailRect.T));

  // Empty state hint.
  if (mRows.empty()) {
    const char* msg;
    if (mAllRows.empty()) {
      msg = mSearch.empty()
          ? "No models yet. Drop .nam files (with their .tone3000.json sidecars) "
            "into your TONE3000 folder and hit Rescan."
          : "No models match the current search.";
    } else {
      msg = "No models match the active filters.";
    }
    g.DrawText(IText(th::kTypeBody, th::kTextMuted,
                     th::kFontBody, EAlign::Center, EVAlign::Middle),
               msg, mGridRect);
  }

  // Detail strip — left side: icon + name + creator + meta.
  if (mSelectedId > 0) {
    const ::t3k::library::ModelRow* row = nullptr;
    for (const auto& r : mRows) {
      if (r.id == mSelectedId) { row = &r; break; }
    }
    if (row) {
      const float iconSz = kDetailH - 2.f * kDetailPad;
      const IRECT iconR(mDetailRect.L + kDetailPad,
                        mDetailRect.T + kDetailPad,
                        mDetailRect.L + kDetailPad + iconSz,
                        mDetailRect.T + kDetailPad + iconSz);
      g.FillRoundRect(th::kBgSurface, iconR, th::kRadiusSm);
      g.DrawRoundRect(th::kBorder,    iconR, th::kRadiusSm, nullptr, 1.f);
      if (ISVG svg = g.LoadSVG(GearIconResource(row->gear_type)); svg.IsValid()) {
        g.DrawSVG(svg, iconR.GetPadded(-10.f));
      }

      const float textL = iconR.R + 12.f;
      const float textR = mLoadBtnRect.L - kDetailPad;
      const IRECT nameR(textL, mDetailRect.T + kDetailPad,
                        textR, mDetailRect.T + kDetailPad + 22.f);
      g.DrawText(IText(16.f, th::kText, th::kFontBodyMed,
                       EAlign::Near, EVAlign::Middle),
                 row->effectiveDisplayName().c_str(), nameR);

      std::string meta;
      if (!row->t3k_creator.empty()) meta = row->t3k_creator;
      if (!row->gear_type.empty()) {
        if (!meta.empty()) meta += " \xC2\xB7 ";
        meta += GearLabelFor(row->gear_type);
      }
      meta += "  ";
      meta += FormatSize(row->size_bytes);
      if (!row->kind.empty()) {
        std::string k = row->kind;
        for (char& c : k) c = static_cast<char>(std::toupper((unsigned char)c));
        meta += " \xC2\xB7 " + k;
      }
      const IRECT metaR(textL, nameR.B + 2.f, textR, nameR.B + 2.f + 16.f);
      g.DrawText(IText(th::kTypeSmall, th::kTextMuted, th::kFontBody,
                       EAlign::Near, EVAlign::Top),
                 meta.c_str(), metaR);

      // Variant count — when this tone has multiple model variants
      // grouped under the card, surface that on the second line so
      // the user knows the detail page has more inside.
      const std::string toneKey = row->t3k_tone_id.empty()
                                    ? ("__local_" + std::to_string(row->id))
                                    : row->t3k_tone_id;
      auto vit = mVariantsByToneId.find(toneKey);
      const size_t variants = (vit != mVariantsByToneId.end())
                                 ? vit->second.size() : 0;
      if (variants > 1) {
        char hint[64];
        std::snprintf(hint, sizeof(hint),
                      "%zu variants \xC2\xB7 double-click to see all",
                      variants);
        const IRECT hintR(textL, metaR.B + 2.f,
                          textR, metaR.B + 2.f + 14.f);
        g.DrawText(IText(th::kTypeSmall, th::kTextDim, th::kFontBody,
                         EAlign::Near, EVAlign::Top),
                   hint, hintR);
      }
    }
  } else {
    g.DrawText(IText(th::kTypeBody, th::kTextDim,
                     th::kFontBody, EAlign::Center, EVAlign::Middle),
               "Click a card to load it into the chain or open its actions.",
               mDetailRect);
  }
}

// ── Sidebar drawContent callbacks ─────────────────────────────────────

void LibraryView::drawGearAccordion(const IRECT& r)
{
  namespace th = ::t3k::theme;
  mGearRowRects.clear();
  auto* gfx = GetUI();
  if (!gfx) return;
  const char* values[] = { "amp", "cab", "outboard", "full-rig", "pedal" };
  float y = r.T + 4.f;
  for (const char* v : values) {
    const IRECT row(r.L + 8.f, y, r.R - 8.f, y + kCheckboxRowH);
    mGearRowRects.emplace_back(v, row);
    const IRECT box(row.L, row.MH() - kCheckboxBoxSz * 0.5f,
                    row.L + kCheckboxBoxSz, row.MH() + kCheckboxBoxSz * 0.5f);
    const bool on = mSelectedGears.count(v) > 0;
    gfx->DrawRect(on ? th::kAccent : th::kBorder, box);
    if (on) gfx->FillRect(th::kAccent, box.GetPadded(-3.f));

    const IRECT lbl(box.R + 8.f, row.T, row.R, row.B);
    gfx->DrawText(IText(th::kTypeBody, th::kText,
                        th::kFontBody, EAlign::Near, EVAlign::Middle),
                  GearLabelFor(v), lbl);
    y += kCheckboxRowH;
  }
}

void LibraryView::drawTagsAccordion(const IRECT& r)
{
  namespace th = ::t3k::theme;
  auto* gfx = GetUI();
  if (!gfx) return;
  const IRECT row(r.L + 8.f, r.T + 4.f, r.R - 8.f, r.T + 4.f + kCheckboxRowH);
  gfx->DrawText(IText(th::kTypeSmall, th::kTextMuted,
                      th::kFontBody, EAlign::Near, EVAlign::Middle),
                "Local tags coming soon", row);
}

void LibraryView::drawMakesAccordion(const IRECT& r)
{
  namespace th = ::t3k::theme;
  mMakeRowRects.clear();
  auto* gfx = GetUI();
  if (!gfx) return;
  if (mAllMakes.empty()) {
    const IRECT row(r.L + 8.f, r.T + 4.f, r.R - 8.f, r.T + 4.f + kCheckboxRowH);
    gfx->DrawText(IText(th::kTypeSmall, th::kTextDim,
                        th::kFontBody, EAlign::Near, EVAlign::Middle),
                  "(no makes in library)", row);
    return;
  }
  float y = r.T + 4.f;
  for (const auto& v : mAllMakes) {
    const IRECT row(r.L + 8.f, y, r.R - 8.f, y + kCheckboxRowH);
    mMakeRowRects.emplace_back(v, row);
    const IRECT box(row.L, row.MH() - kCheckboxBoxSz * 0.5f,
                    row.L + kCheckboxBoxSz, row.MH() + kCheckboxBoxSz * 0.5f);
    const bool on = mSelectedMakes.count(v) > 0;
    gfx->DrawRect(on ? th::kAccent : th::kBorder, box);
    if (on) gfx->FillRect(th::kAccent, box.GetPadded(-3.f));

    const IRECT lbl(box.R + 8.f, row.T, row.R, row.B);
    gfx->DrawText(IText(th::kTypeBody, th::kText,
                        th::kFontBody, EAlign::Near, EVAlign::Middle),
                  v.c_str(), lbl);
    y += kCheckboxRowH;
  }
}

void LibraryView::drawCreatorsAccordion(const IRECT& r)
{
  namespace th = ::t3k::theme;
  mCreatorRowRects.clear();
  auto* gfx = GetUI();
  if (!gfx) return;
  if (mAllCreators.empty()) {
    const IRECT row(r.L + 8.f, r.T + 4.f, r.R - 8.f, r.T + 4.f + kCheckboxRowH);
    gfx->DrawText(IText(th::kTypeSmall, th::kTextDim,
                        th::kFontBody, EAlign::Near, EVAlign::Middle),
                  "(no creators in library)", row);
    return;
  }
  float y = r.T + 4.f;
  for (const auto& v : mAllCreators) {
    const IRECT row(r.L + 8.f, y, r.R - 8.f, y + kCheckboxRowH);
    mCreatorRowRects.emplace_back(v, row);
    const IRECT box(row.L, row.MH() - kCheckboxBoxSz * 0.5f,
                    row.L + kCheckboxBoxSz, row.MH() + kCheckboxBoxSz * 0.5f);
    const bool on = mSelectedCreators.count(v) > 0;
    gfx->DrawRect(on ? th::kAccent : th::kBorder, box);
    if (on) gfx->FillRect(th::kAccent, box.GetPadded(-3.f));

    const IRECT lbl(box.R + 8.f, row.T, row.R, row.B);
    gfx->DrawText(IText(th::kTypeBody, th::kText,
                        th::kFontBody, EAlign::Near, EVAlign::Middle),
                  v.c_str(), lbl);
    y += kCheckboxRowH;
  }
}

void LibraryView::drawTechnicalAccordion(const IRECT& r)
{
  namespace th = ::t3k::theme;
  mFormatRowRects.clear();
  auto* gfx = GetUI();
  if (!gfx) return;
  // The local library carries NAM models + IR .wav files — surface
  // those as a format filter under "Technical". Mirrors Cloud's Size
  // filter slot.
  const char* values[] = { "nam", "ir" };
  const char* labels[] = { "NAM model", "Impulse response (IR)" };
  float y = r.T + 4.f;
  for (size_t i = 0; i < 2; ++i) {
    const std::string v = values[i];
    const IRECT row(r.L + 8.f, y, r.R - 8.f, y + kCheckboxRowH);
    mFormatRowRects.emplace_back(v, row);
    const IRECT box(row.L, row.MH() - kCheckboxBoxSz * 0.5f,
                    row.L + kCheckboxBoxSz, row.MH() + kCheckboxBoxSz * 0.5f);
    const bool on = mSelectedFormats.count(v) > 0;
    gfx->DrawRect(on ? th::kAccent : th::kBorder, box);
    if (on) gfx->FillRect(th::kAccent, box.GetPadded(-3.f));

    const IRECT lbl(box.R + 8.f, row.T, row.R, row.B);
    gfx->DrawText(IText(th::kTypeBody, th::kText,
                        th::kFontBody, EAlign::Near, EVAlign::Middle),
                  labels[i], lbl);
    y += kCheckboxRowH;
  }
}

}  // namespace t3k::ui
