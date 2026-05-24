// CloudView.cpp — Phase 6 cloud-browser tab. See CloudView.h.

#include "CloudView.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

#include "IGraphics.h"

#include "../theme.h"
#include "../layout.h"
#include "../controls/T3kAccordion.h"
#include "../controls/T3kButton.h"
#include "../controls/T3kCard.h"
#include "../controls/T3kSearchBar.h"
#include "../controls/T3kSignInPill.h"
#include "T3kDetailModal.h"

#include "../../cloud/Downloader.h"
#include "../../cloud/OAuthFlow.h"
#include "../../cloud/Session.h"
#include "../../cloud/SessionEvent.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// Layout constants — tuned for the 1280×800 default window.
constexpr float kHeaderH       = 56.f;
constexpr float kHeaderPad     = 16.f;
constexpr float kSearchH       = 32.f;
constexpr float kSortBtnW      = 150.f;
constexpr float kSortBtnH      = 28.f;
constexpr float kSidebarW      = 220.f;
constexpr float kSidebarPad    = 12.f;
constexpr float kAccordionH    = 200.f;   // when expanded — header + ~5 rows
constexpr float kCheckboxRowH  = 26.f;
constexpr float kCheckboxBoxSz = 14.f;
constexpr float kCardH         = 132.f;   // T3kCard tuned for 88px image
constexpr float kCardGapY      = 8.f;
constexpr float kBodyPad       = 16.f;
// Reserve a row at the top of the body for the download / loading
// status banner. Always present (not toggled on activity) so the
// cards below it don't jitter every time a download starts. The row
// renders empty when there's no message — fine, it just becomes
// transparent space.
constexpr float kStatusBannerH = 22.f;

// Cycle through the TonesSort enum on each sort-button click.
::t3k::cloud::TonesSort nextSort(::t3k::cloud::TonesSort s) {
  using S = ::t3k::cloud::TonesSort;
  switch (s) {
    case S::BestMatch:        return S::Newest;
    case S::Newest:           return S::Oldest;
    case S::Oldest:           return S::Trending;
    case S::Trending:         return S::DownloadsAllTime;
    case S::DownloadsAllTime: return S::BestMatch;
  }
  return S::BestMatch;
}

// Bucketize an ISO-8601 timestamp into a coarse "X weeks ago" string.
// Cheap parse — only the date portion matters, and we deliberately
// ignore TZ because the catalog returns UTC. Returns "" on parse fail.
std::string renderRelative(const std::string& iso) {
  if (iso.size() < 10) return "";
  int y = 0, m = 0, d = 0;
  if (std::sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return "";
  // Howard Hinnant's days_from_civil — closed form, no <chrono> calendar.
  y -= m <= 2 ? 1 : 0;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long long days_since_epoch =
      era * 146097LL + static_cast<long long>(doe) - 719468LL;

  const long long now_days = static_cast<long long>(std::time(nullptr) / 86400);
  long long delta = now_days - days_since_epoch;
  if (delta < 1)   return "today";
  if (delta < 2)   return "yesterday";
  if (delta < 7)   return std::to_string(delta) + " days ago";
  if (delta < 30) {
    const long long weeks = delta / 7;
    return std::to_string(weeks) + (weeks == 1 ? " week ago" : " weeks ago");
  }
  if (delta < 365) {
    const long long months = delta / 30;
    return std::to_string(months) + (months == 1 ? " month ago" : " months ago");
  }
  const long long years = delta / 365;
  return std::to_string(years) + (years == 1 ? " year ago" : " years ago");
}

// Convert a Tone into a T3kCard::CardData snapshot for display.
T3kCard::CardData makeCardData(const ::t3k::cloud::Tone& t) {
  T3kCard::CardData d;
  d.title = t.title;
  // Subtitle: gear · first three tags.
  std::string sub = ::t3k::cloud::toLabel(t.gear);
  if (!t.tags.empty()) {
    sub += " \xC2\xB7 ";
    for (size_t i = 0; i < std::min<size_t>(t.tags.size(), 3); ++i) {
      if (i) sub += ", ";
      sub += t.tags[i].name;
    }
  }
  d.subtitle = std::move(sub);
  d.creator  = t.user.username;
  // Platform badge — uppercase the wire value for visual contrast.
  std::string badge = ::t3k::cloud::toWire(t.platform);
  for (auto& c : badge) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  d.badgeText    = std::move(badge);
  d.image        = std::nullopt;
  if (t.images.has_value() && !t.images->empty()) {
    d.imageUrl = (*t.images)[0];
  }
  d.downloads    = t.downloads_count;
  d.bookmarks    = t.favorites_count;
  d.modelCount   = t.models_count;
  d.relativeDate = t.updated_at.has_value() ? renderRelative(*t.updated_at) : "";
  return d;
}

}  // namespace

CloudView::CloudView(const IRECT& bounds)
: IControl(bounds)
{
  OnResize();
}

CloudView::~CloudView()
{
  if (mSessionListenerId > 0) {
    ::t3k::cloud::Session::instance().unsubscribe(mSessionListenerId);
    mSessionListenerId = 0;
  }
  if (mDlListenerId > 0) {
    ::t3k::cloud::Downloader::instance().unsubscribe(mDlListenerId);
    mDlListenerId = 0;
  }
  // Cancel anything in flight so the worker's completion can't fire
  // into a destroyed `this`.
  mPendingToken.cancel();
}

void CloudView::Hide(bool hide)
{
  IControl::Hide(hide);
  if (mSearchBar)   mSearchBar  ->Hide(hide);
  if (mSortBtn)     mSortBtn    ->Hide(hide);
  if (mGearAcc)     mGearAcc    ->Hide(hide);
  if (mTagsAcc)     mTagsAcc    ->Hide(hide);
  if (mMakesAcc)    mMakesAcc   ->Hide(hide);
  if (mCreatorsAcc) mCreatorsAcc->Hide(hide);
  if (mTechAcc)     mTechAcc    ->Hide(hide);
  if (mSignInPill)  mSignInPill ->Hide(hide || mState != State::SignedOut);
  if (mDetailModal) mDetailModal->Hide(true);  // always closed on tab switch
  for (T3kCard* c : mCards) if (c) c->Hide(hide);
}

void CloudView::OnResize()
{
  namespace th = ::t3k::theme;

  auto hsplit = t3k::layout::rowFixedTop(mRECT, kHeaderH);
  mHeaderRect = hsplit.first;
  IRECT below = hsplit.second;

  const float left  = mHeaderRect.L + kHeaderPad;
  const float right = mHeaderRect.R - kHeaderPad;
  const float midY  = mHeaderRect.MH();
  const float searchTop = midY - kSearchH * 0.5f;
  const float btnTop    = midY - kSortBtnH * 0.5f;

  mSortRect   = IRECT(right - kSortBtnW, btnTop,
                      right,             btnTop + kSortBtnH);
  mSearchRect = IRECT(left, searchTop,
                      mSortRect.L - th::kS3,
                      searchTop + kSearchH);

  mSidebarRect = IRECT(below.L, below.T, below.L + kSidebarW, below.B);
  mBodyRect    = IRECT(mSidebarRect.R, below.T, below.R, below.B);

  if (mSearchBar) mSearchBar->SetTargetAndDrawRECTs(mSearchRect);
  if (mSortBtn)   mSortBtn  ->SetTargetAndDrawRECTs(mSortRect);

  // Position the 5 sidebar accordions based on which ones are open.
  // Closed accordions take only their header height; open ones add
  // their content height. layoutSidebar walks the list top-to-bottom.
  layoutSidebar();

  if (mSignInPill) {
    const float pillW = 140.f, pillH = 32.f;
    const float cx = mBodyRect.MW(), cy = mBodyRect.MH() + 30.f;
    mSignInPill->SetTargetAndDrawRECTs(
        IRECT(cx - pillW * 0.5f, cy - pillH * 0.5f,
              cx + pillW * 0.5f, cy + pillH * 0.5f));
  }

  layoutCards();
}

void CloudView::layoutSidebar()
{
  namespace th = ::t3k::theme;
  // Walk the 5 accordions top-to-bottom, stacking each at the next
  // free Y. A collapsed accordion contributes only its header height
  // (~36px); an expanded one adds its content height as well.
  T3kAccordion* accs[] = {
      mGearAcc, mTagsAcc, mMakesAcc, mCreatorsAcc, mTechAcc,
  };
  // Per-accordion content height when expanded. Indices match accs[].
  const float contentH[] = {
      5 * kCheckboxRowH + 8.f,  // Gear: 5 enum values
      kCheckboxRowH    + 8.f,   // Tags: single "coming soon" line
      kCheckboxRowH    + 8.f,   // Makes & Models: same
      kCheckboxRowH    + 8.f,   // Creators: same
      5 * kCheckboxRowH + 8.f,  // Technical: 5 size enum values
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

void CloudView::OnAttached()
{
  IGraphics* g = GetUI();
  if (!g) return;

  mSearchBar = new T3kSearchBar(mSearchRect,
      [this](const std::string& q) { this->onSearchChanged(q); },
      "Search the TONE3000 catalog\xE2\x80\xA6");
  g->AttachControl(mSearchBar);

  mSortBtn = new T3kButton(mSortRect, ::t3k::cloud::toLabel(mQuery.sort),
      [this]() { this->onSortClicked(); },
      T3kButton::Variant::Secondary);
  g->AttachControl(mSortBtn);

  // Helper to wire the shared onToggle handler — every accordion needs
  // to re-run layoutSidebar after the user flips it open/closed.
  auto wireToggle = [this](T3kAccordion* acc) {
    acc->setOnToggle([this](bool /*isOpen*/) {
      this->layoutSidebar();
      this->SetDirty(false);
    });
  };

  // Bounds get fixed up by layoutSidebar() below — pass a 1×1 placeholder
  // here so iPlug2 has a valid rect at attach time.
  const IRECT placeholder(0.f, 0.f, 1.f, 1.f);

  mGearAcc = new T3kAccordion(placeholder,
      "Gear",
      []() { return 5 * kCheckboxRowH + 8.f; },
      [this](const IRECT& r) { this->drawGearAccordion(r); },
      /*initiallyOpen*/ true);
  wireToggle(mGearAcc);
  g->AttachControl(mGearAcc);

  // Tags / Makes and Models / Creators accordions — hidden from the UI
  // per the 2026-05-25 polish round. The public Tone3000 SDK doesn't
  // yet expose query params for these filter dimensions, so until the
  // backend catches up they served only as placeholders. Flip the
  // toggle to re-enable.
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
        []() { return kCheckboxRowH + 8.f; },
        [this](const IRECT& r) { this->drawMakesAccordion(r); },
        /*initiallyOpen*/ false);
    wireToggle(mMakesAcc);
    g->AttachControl(mMakesAcc);

    mCreatorsAcc = new T3kAccordion(placeholder,
        "Creators",
        []() { return kCheckboxRowH + 8.f; },
        [this](const IRECT& r) { this->drawCreatorsAccordion(r); },
        /*initiallyOpen*/ false);
    wireToggle(mCreatorsAcc);
    g->AttachControl(mCreatorsAcc);
  }

  mTechAcc = new T3kAccordion(placeholder,
      "Technical",
      []() { return 5 * kCheckboxRowH + 8.f; },
      [this](const IRECT& r) { this->drawTechnicalAccordion(r); },
      /*initiallyOpen*/ false);
  wireToggle(mTechAcc);
  g->AttachControl(mTechAcc);

  mSignInPill = new T3kSignInPill(
      IRECT(0.f, 0.f, 1.f, 1.f),
      /*onClick*/ [this] { this->onSignInClicked(); });
  g->AttachControl(mSignInPill);

  // Detail modal — full-window overlay attached last so the z-order
  // keeps it on top of cards. Hidden by default; cards' setOnDetail
  // dispatch flips it on.
  mDetailModal = new T3kDetailModal(mRECT,
      [this]() {
        if (mDetailModal) mDetailModal->Hide(true);
      });
  g->AttachControl(mDetailModal);
  mDetailModal->Hide(true);

  OnResize();

  // Session listener — flip the empty/loaded state when the user
  // signs in or out mid-session. Body of the callback just marks
  // dirty; the next Draw cycle reads Session::state() under its own
  // mutex.
  mSessionListenerId = ::t3k::cloud::Session::instance().subscribe(
      [this](const ::t3k::cloud::SessionEvent& /*ev*/) {
        SetDirty(false);
      });

  // Downloader listener — refresh the bottom status banner as
  // downloads move through their pipeline. Worker-thread callback;
  // stash the text + expiry under the mutex and let the next paint
  // drain it (the SetDirty marker triggers a redraw).
  mDlListenerId = ::t3k::cloud::Downloader::instance().subscribe(
      [this](const ::t3k::cloud::DownloadStatus& s) {
        using Stage = ::t3k::cloud::DownloadStatus::Stage;
        std::string msg;
        std::chrono::seconds ttl{8};

        // Per-card pill state for the in-card status fix. Done /
        // Failed land as the card's permanent visual until the next
        // search rebuilds cards — so the user can see at a glance
        // which tones they've already grabbed.
        T3kCard::DownloadState cardState = T3kCard::DownloadState::Active;
        std::string cardLabel;

        switch (s.stage) {
          case Stage::Queued:
            msg = "Queued: " + s.tone_title;
            cardLabel = "QUEUED";
            break;
          case Stage::Listing:
            msg = "Listing models: " + s.tone_title;
            cardLabel = "LISTING\xE2\x80\xA6";
            break;
          case Stage::Downloading: {
            const int n = s.model_index + 1;
            const int t = std::max(1, s.total_models);
            msg = "Downloading " + s.tone_title +
                  " (" + std::to_string(n) + " of " +
                  std::to_string(t) + ")\xE2\x80\xA6";
            cardLabel = std::to_string(n) + "/" + std::to_string(t);
            break;
          }
          case Stage::Writing:
            msg = "Saving: " + s.tone_title;
            cardLabel = "SAVING\xE2\x80\xA6";
            break;
          case Stage::Done:
            msg = "Downloaded: " + s.tone_title;
            ttl = std::chrono::seconds{5};
            cardState = T3kCard::DownloadState::Done;
            cardLabel.clear();        // pill uses the default "DOWNLOADED"
            break;
          case Stage::Failed:
            msg = "Failed: " + s.tone_title +
                  (s.error_message.empty() ? std::string{}
                                           : (" \xE2\x80\x94 " + s.error_message));
            ttl = std::chrono::seconds{12};
            cardState = T3kCard::DownloadState::Failed;
            cardLabel.clear();        // pill uses the default "FAILED"
            break;
        }
        {
          std::lock_guard<std::mutex> lk(mDlStatusMtx);
          mDlStatusText   = std::move(msg);
          mDlStatusExpiry = std::chrono::steady_clock::now() + ttl;
          mDlByToneId[s.tone_id] = ToneDlState{cardState, std::move(cardLabel)};
        }
        this->SetDirty(false);
      });

  refreshFromSession();
}

void CloudView::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Background + 1px header divider.
  g.FillRect(th::kBgBase, mRECT);
  g.FillRect(th::kBorder,
             IRECT(mRECT.L, mHeaderRect.B - 1.f, mRECT.R, mHeaderRect.B));
  // Sidebar divider.
  g.FillRect(th::kBorder,
             IRECT(mSidebarRect.R, mSidebarRect.T,
                   mSidebarRect.R + 1.f, mSidebarRect.B));

  drainPendingResult();

  // Drain the latest Downloader status into BOTH the per-card pill
  // states (primary visibility surface — pill colors + label) and
  // the top-of-body summary banner (mErrorMessage). Held under
  // mDlStatusMtx because the listener fires from the HTTP worker
  // thread.
  {
    std::lock_guard<std::mutex> lk(mDlStatusMtx);

    // Per-card update — find the card for each tone_id we have a
    // status for and push the latest state into its DOWNLOAD pill.
    for (const auto& kv : mDlByToneId) {
      auto it = mToneIdToCardIdx.find(kv.first);
      if (it == mToneIdToCardIdx.end()) continue;
      const std::size_t cardIdx = it->second;
      if (cardIdx >= mCards.size() || !mCards[cardIdx]) continue;
      mCards[cardIdx]->setDownloadState(kv.second.state, kv.second.label);
    }

    // Top-of-body banner — expires after the TTL the listener set
    // so a stale "Downloaded: …" doesn't linger forever.
    if (!mDlStatusText.empty()) {
      if (std::chrono::steady_clock::now() < mDlStatusExpiry) {
        mErrorMessage = mDlStatusText;
      } else {
        mDlStatusText.clear();
        mErrorMessage.clear();
      }
    }
  }

  // Re-sync state from Session — catches sign-in/out done from
  // another tab while we weren't visible.
  {
    const auto s = ::t3k::cloud::Session::instance().state();
    const bool signedIn = (s == ::t3k::cloud::Session::State::SignedIn);
    if (signedIn && mState == State::SignedOut) {
      refreshFromSession();
    } else if (!signedIn && mState != State::SignedOut) {
      mState = State::SignedOut;
      mTones.clear();
      for (T3kCard* c : mCards) if (c) c->Hide(true);
    }
  }
  if (mSignInPill)  mSignInPill ->Hide(mState != State::SignedOut);
  const bool hideFilters = (mState == State::SignedOut);
  if (mGearAcc)     mGearAcc    ->Hide(hideFilters);
  if (mTagsAcc)     mTagsAcc    ->Hide(hideFilters);
  if (mMakesAcc)    mMakesAcc   ->Hide(hideFilters);
  if (mCreatorsAcc) mCreatorsAcc->Hide(hideFilters);
  if (mTechAcc)     mTechAcc    ->Hide(hideFilters);

  drawStateOverlay(g);
}

void CloudView::drainPendingResult()
{
  if (!mResultReady.load(std::memory_order_acquire)) return;

  ::t3k::cloud::ToneSearchResult res;
  bool isAppend = false;
  {
    std::lock_guard<std::mutex> lk(mResultMtx);
    res      = std::move(mPendingResult);
    isAppend = mPendingResultIsAppend;
    mResultReady.store(false, std::memory_order_release);
  }

  if (!res.success) {
    mState = State::Error;
    mErrorMessage = res.error_message.empty()
                      ? std::string("Request failed")
                      : res.error_message;
    SetDirty(false);
    return;
  }

  mTotalPages = res.data.total_pages;
  mHasMore    = res.data.hasNextPage();
  mNextPage   = res.data.page + 1;

  if (!isAppend) {
    mTones = std::move(res.data.data);
    mScrollOffset = 0.f;
  } else {
    for (auto& t : res.data.data) mTones.push_back(std::move(t));
  }
  mState = State::Loaded;
  rebuildCards();
  layoutCards();
  SetDirty(false);
}

void CloudView::refreshFromSession()
{
  const auto s = ::t3k::cloud::Session::instance().state();
  const bool signedIn = (s == ::t3k::cloud::Session::State::SignedIn);
  if (!signedIn) {
    mState = State::SignedOut;
    SetDirty(false);
    return;
  }
  // Signed in. If we already have results, just stay in Loaded; if
  // not, kick a fresh search with the current (defaults-to-Trending)
  // query so the user sees something the moment they hit the tab —
  // matches tone3000.com/search's empty-query landing UX.
  if (!mTones.empty()) {
    mState = State::Loaded;
    SetDirty(false);
    return;
  }
  startSearch();
}

void CloudView::OnMouseDown(float x, float y, const IMouseMod& /*mod*/)
{
  if (mState != State::SignedOut && handleSidebarClick(x, y)) return;
  if (mState == State::Error && mBodyRect.Contains(x, y)) {
    startSearch();
  }
}

void CloudView::OnMouseWheel(float /*x*/, float /*y*/,
                             const IMouseMod& /*mod*/, float d)
{
  scrollBy(d);
}

void CloudView::scrollBy(float d)
{
  if (mState != State::Loaded) return;
  mScrollOffset -= d * 80.f;
  if (mScrollOffset < 0.f) mScrollOffset = 0.f;

  const float totalH = static_cast<float>(mTones.size()) * (kCardH + kCardGapY);
  const float maxOffset = std::max(0.f, totalH - mBodyRect.H() + 32.f);
  if (mScrollOffset > maxOffset) mScrollOffset = maxOffset;

  const float bottomGap = totalH - mScrollOffset - mBodyRect.H();
  if (mHasMore && bottomGap < kCardH * 1.5f) {
    fetchNextPage();
  }

  layoutCards();
  SetDirty(false);
}

bool CloudView::handleSidebarClick(float x, float y)
{
  for (const auto& pair : mGearRowRects) {
    if (pair.second.Contains(x, y)) { toggleGear(pair.first); return true; }
  }
  for (const auto& pair : mSizeRowRects) {
    if (pair.second.Contains(x, y)) { toggleSize(pair.first); return true; }
  }
  return false;
}

void CloudView::toggleGear(::t3k::cloud::Gear g)
{
  if (auto it = mSelectedGears.find(g); it == mSelectedGears.end())
    mSelectedGears.insert(g);
  else
    mSelectedGears.erase(it);
  startSearch();
}

void CloudView::toggleSize(::t3k::cloud::Size s)
{
  if (auto it = mSelectedSizes.find(s); it == mSelectedSizes.end())
    mSelectedSizes.insert(s);
  else
    mSelectedSizes.erase(it);
  startSearch();
}

void CloudView::onSearchChanged(const std::string& q)
{
  mQuery.query = q;
  startSearch();
}

void CloudView::onSortClicked()
{
  mQuery.sort = nextSort(mQuery.sort);
  if (mSortBtn) mSortBtn->setLabel(::t3k::cloud::toLabel(mQuery.sort));
  startSearch();
}

void CloudView::onSignInClicked()
{
  if (::t3k::cloud::OAuthFlow::isConfigured()) {
    ::t3k::cloud::Session::instance().signIn();
  } else {
    mErrorMessage = "OAuth client_id not configured — see OAuthConfig.h";
    mState = State::Error;
    SetDirty(false);
  }
}

void CloudView::onCardSelected(int toneIndex)
{
  for (size_t i = 0; i < mCards.size(); ++i) {
    if (mCards[i]) mCards[i]->setSelected(static_cast<int>(i) == toneIndex);
  }
  SetDirty(false);
}

void CloudView::onCardDownload(int toneIndex)
{
  if (toneIndex < 0 || toneIndex >= static_cast<int>(mTones.size())) return;
  const auto& tone = mTones[toneIndex];

  // enqueueTone publishes the Queued event synchronously inside
  // this call → the listener stashes mDlByToneId + mDlStatusText
  // before we return → the next paint cycle drains both into the
  // card's pill (turns kAccent, label "QUEUED") and the top-of-body
  // banner. Click feedback is therefore one frame away — no manual
  // mErrorMessage set needed.
  ::t3k::cloud::Downloader::instance().enqueueTone(tone);
  SetDirty(false);
}

void CloudView::onCardDetail(int toneIndex)
{
  if (toneIndex < 0 || toneIndex >= static_cast<int>(mTones.size())) return;
  // Cards z-order > modal because cards attach lazily during
  // rebuildCards (after OnAttached). Recreate the modal on top so it
  // paints above them.
  if (IGraphics* g = GetUI()) {
    if (mDetailModal) {
      mDetailModal->detachAllChildren();
      g->RemoveControl(mDetailModal);
      mDetailModal = nullptr;
    }
    mDetailModal = new T3kDetailModal(mRECT,
        [this]() {
          if (mDetailModal) mDetailModal->Hide(true);
          SetDirty(false);
        });
    g->AttachControl(mDetailModal);
    mDetailModal->Hide(true);
  }
  if (!mDetailModal) return;
  const auto& tone = mTones[toneIndex];

  T3kDetailModal::DetailData d;
  d.title       = tone.title;
  d.creator     = tone.user.username;
  d.description = tone.description.value_or("");
  // Subtitle: gear-label · platform / first size.
  d.subtitle    = ::t3k::cloud::toLabel(tone.gear);
  if (!tone.sizes.empty()) {
    d.subtitle += "  \xC2\xB7  ";
    d.subtitle += ::t3k::cloud::toLabel(tone.sizes.front());
  }
  if (tone.images.has_value() && !tone.images->empty()) {
    // Cloud's hosted image URL — T3kDetailModal currently only renders
    // local paths so the cloud detail page falls back to the "no image"
    // placeholder until ThumbnailCache picks up the URL.
    d.imageUrl = (*tone.images)[0];
  }
  for (const auto& t : tone.tags)  d.tags.push_back(t.name);
  for (const auto& m : tone.makes) d.makesModels.push_back(m.name);

  // Cloud actions — Download only. Selecting / dismissing the modal
  // closes it.
  std::vector<T3kDetailModal::Action> actions = {
    { "DOWNLOAD", /*primary*/ true, [this, toneIndex]() {
        if (mDetailModal) mDetailModal->Hide(true);
        this->onCardDownload(toneIndex);
      } },
  };
  mDetailModal->show(std::move(d), std::move(actions));
}

void CloudView::startSearch()
{
  if (mState == State::SignedOut) return;
  mPendingToken.cancel();
  mNextPage = 1;
  mQuery.page = 1;
  mQuery.page_size = 25;
  mQuery.gears.assign(mSelectedGears.begin(), mSelectedGears.end());
  mQuery.sizes.assign(mSelectedSizes.begin(), mSelectedSizes.end());

  mState = State::Loading;
  mErrorMessage.clear();
  SetDirty(false);

  mPendingToken = ::t3k::cloud::Tone3000Client::instance().searchTones(
      mQuery,
      [this](::t3k::cloud::ToneSearchResult r) {
        std::lock_guard<std::mutex> lk(mResultMtx);
        mPendingResult = std::move(r);
        mPendingResultIsAppend = false;
        mResultReady.store(true, std::memory_order_release);
      });
}

void CloudView::fetchNextPage()
{
  if (!mHasMore) return;
  if (mState == State::Loading) return;
  ::t3k::cloud::SearchTonesParams p = mQuery;
  p.page = mNextPage;
  mState = State::Loading;
  SetDirty(false);

  mPendingToken = ::t3k::cloud::Tone3000Client::instance().searchTones(
      p,
      [this](::t3k::cloud::ToneSearchResult r) {
        std::lock_guard<std::mutex> lk(mResultMtx);
        mPendingResult = std::move(r);
        mPendingResultIsAppend = true;
        mResultReady.store(true, std::memory_order_release);
      });
}

void CloudView::rebuildCards()
{
  IGraphics* g = GetUI();
  if (!g) return;

  while (mCards.size() < mTones.size()) {
    const int idx = static_cast<int>(mCards.size());
    auto* c = new T3kCard(
        IRECT(0.f, 0.f, 1.f, 1.f),
        makeCardData(mTones[idx]),
        /*onSelect*/   [this, idx]() { this->onCardSelected(idx); },
        /*onDownload*/ [this, idx]() { this->onCardDownload(idx); });
    // Forward scroll-wheel events from this card up to CloudView's
    // scroll handler. Without this, iPlug2 dispatches the wheel to
    // the card (topmost control under the cursor) and our scrollBy
    // never runs — the list reads as unscrollable.
    c->setOnWheel([this](float d) { this->scrollBy(d); });
    c->setOnDetail([this, idx]() { this->onCardDetail(idx); });
    g->AttachControl(c);
    mCards.push_back(c);
  }

  // If mTones shrunk, hide the surplus IControls but keep them around —
  // RemoveControl frees, and rebuilding cards on every fresh search
  // would churn iPlug2's control list.
  for (std::size_t i = mTones.size(); i < mCards.size(); ++i) {
    if (mCards[i]) mCards[i]->Hide(true);
  }

  // Rebuild tone_id → card index map and restore each card's
  // download-state from the (sticky-across-searches) mDlByToneId
  // map. Without restore, a Done card that scrolls out of view and
  // back in via a fresh search would forget it's already downloaded.
  mToneIdToCardIdx.clear();
  mToneIdToCardIdx.reserve(mTones.size());
  std::lock_guard<std::mutex> lk(mDlStatusMtx);
  for (std::size_t i = 0; i < mTones.size(); ++i) {
    const int tid = mTones[i].id;
    mToneIdToCardIdx[tid] = i;
    if (auto it = mDlByToneId.find(tid); it != mDlByToneId.end()) {
      if (mCards[i]) {
        mCards[i]->setDownloadState(it->second.state, it->second.label);
      }
    } else if (mCards[i]) {
      mCards[i]->setDownloadState(T3kCard::DownloadState::Idle);
    }
  }
}

void CloudView::layoutCards()
{
  if (mCards.empty()) return;

  const float bodyLeft  = mBodyRect.L + kBodyPad;
  const float bodyRight = mBodyRect.R - kBodyPad;

  for (size_t i = 0; i < mCards.size(); ++i) {
    T3kCard* c = mCards[i];
    if (!c) continue;
    if (i >= mTones.size()) { c->Hide(true); continue; }

    // Cards start kStatusBannerH below the top of the body so the
    // top status banner has its own row.
    const float cardsTop = mBodyRect.T + kStatusBannerH + kBodyPad;
    const float top = cardsTop
                      + static_cast<float>(i) * (kCardH + kCardGapY)
                      - mScrollOffset;
    const float bot = top + kCardH;

    // Logical (un-clamped) rect — used by T3kCard for content
    // positioning so the image / title / pill slide smoothly with
    // scroll instead of distorting when the card crosses the top
    // edge of the body.
    const IRECT logical(bodyLeft, top, bodyRight, bot);

    // Paint rect — clamped to the cards area. iPlug2 scissors each
    // control to its mRECT, so this clamping is what prevents the
    // card from painting into the header / search-bar area when
    // scrolled. Content positioned by the LOGICAL rect overflows
    // the clamped area at the top, and iPlug2's scissor clips it.
    const IRECT clipped(bodyLeft,
                        std::max(top, cardsTop),
                        bodyRight,
                        std::min(bot, mBodyRect.B));

    c->setLogicalRect(logical);
    c->SetTargetAndDrawRECTs(clipped);

    const bool offscreen = (clipped.B <= clipped.T) ||
                           (bot < mBodyRect.T) ||
                           (top > mBodyRect.B);
    c->Hide(offscreen || mState == State::SignedOut);
  }
}

void CloudView::drawGearAccordion(const IRECT& r)
{
  namespace th = ::t3k::theme;
  mGearRowRects.clear();
  const ::t3k::cloud::Gear all[] = {
    ::t3k::cloud::Gear::Amp, ::t3k::cloud::Gear::FullRig,
    ::t3k::cloud::Gear::Pedal, ::t3k::cloud::Gear::Outboard,
    ::t3k::cloud::Gear::Ir,
  };
  float y = r.T + 4.f;
  auto* gfx = GetUI();
  if (!gfx) return;
  for (auto gv : all) {
    const IRECT row(r.L + 8.f, y, r.R - 8.f, y + kCheckboxRowH);
    mGearRowRects.emplace_back(gv, row);

    const IRECT box(row.L, row.MH() - kCheckboxBoxSz * 0.5f,
                    row.L + kCheckboxBoxSz, row.MH() + kCheckboxBoxSz * 0.5f);
    const bool on = mSelectedGears.count(gv) > 0;
    gfx->DrawRect(on ? th::kAccent : th::kBorder, box);
    if (on) gfx->FillRect(th::kAccent, box.GetPadded(-3.f));

    const IRECT lbl(box.R + 8.f, row.T, row.R, row.B);
    gfx->DrawText(IText(th::kTypeBody, th::kText,
                        th::kFontBody, EAlign::Near, EVAlign::Middle),
                  ::t3k::cloud::toLabel(gv), lbl);
    y += kCheckboxRowH;
  }
}

void CloudView::drawTechnicalAccordion(const IRECT& r)
{
  // The "Technical" accordion currently houses only the Size filter
  // (the public Tone3000 SDK's `SearchTonesParams.sizes` field). The
  // tone3000.com Technical drawer also exposes platform, license,
  // sample-rate and license filters — none of which are surfaced by
  // the public SDK at this revision, so they're deferred to a future
  // round once we know how to send those params.
  namespace th = ::t3k::theme;
  mSizeRowRects.clear();
  const ::t3k::cloud::Size all[] = {
    ::t3k::cloud::Size::Standard, ::t3k::cloud::Size::Lite,
    ::t3k::cloud::Size::Feather,  ::t3k::cloud::Size::Nano,
    ::t3k::cloud::Size::Custom,
  };
  float y = r.T + 4.f;
  auto* gfx = GetUI();
  if (!gfx) return;
  for (auto sv : all) {
    const IRECT row(r.L + 8.f, y, r.R - 8.f, y + kCheckboxRowH);
    mSizeRowRects.emplace_back(sv, row);

    const IRECT box(row.L, row.MH() - kCheckboxBoxSz * 0.5f,
                    row.L + kCheckboxBoxSz, row.MH() + kCheckboxBoxSz * 0.5f);
    const bool on = mSelectedSizes.count(sv) > 0;
    gfx->DrawRect(on ? th::kAccent : th::kBorder, box);
    if (on) gfx->FillRect(th::kAccent, box.GetPadded(-3.f));

    const IRECT lbl(box.R + 8.f, row.T, row.R, row.B);
    gfx->DrawText(IText(th::kTypeBody, th::kText,
                        th::kFontBody, EAlign::Near, EVAlign::Middle),
                  ::t3k::cloud::toLabel(sv), lbl);
    y += kCheckboxRowH;
  }
}

// ── Placeholder accordions ─────────────────────────────────────────
// Tags / Makes-and-Models / Creators are documented filter dimensions
// on tone3000.com/search but the public Tone3000 SDK
// (tone-3000/api/src/types.ts) doesn't yet expose query params for
// them on /tones/search. We ship the UI shell so the layout matches
// the website, and surface a "coming soon" line until the API does.

void CloudView::drawTagsAccordion(const IRECT& r)
{
  namespace th = ::t3k::theme;
  auto* gfx = GetUI();
  if (!gfx) return;
  const IRECT row(r.L + 8.f, r.T + 4.f, r.R - 8.f, r.T + 4.f + kCheckboxRowH);
  gfx->DrawText(IText(th::kTypeSmall, th::kTextMuted,
                      th::kFontBody, EAlign::Near, EVAlign::Middle),
                "Tag filter coming soon", row);
}

void CloudView::drawMakesAccordion(const IRECT& r)
{
  namespace th = ::t3k::theme;
  auto* gfx = GetUI();
  if (!gfx) return;
  const IRECT row(r.L + 8.f, r.T + 4.f, r.R - 8.f, r.T + 4.f + kCheckboxRowH);
  gfx->DrawText(IText(th::kTypeSmall, th::kTextMuted,
                      th::kFontBody, EAlign::Near, EVAlign::Middle),
                "Make/model filter coming soon", row);
}

void CloudView::drawCreatorsAccordion(const IRECT& r)
{
  namespace th = ::t3k::theme;
  auto* gfx = GetUI();
  if (!gfx) return;
  const IRECT row(r.L + 8.f, r.T + 4.f, r.R - 8.f, r.T + 4.f + kCheckboxRowH);
  gfx->DrawText(IText(th::kTypeSmall, th::kTextMuted,
                      th::kFontBody, EAlign::Near, EVAlign::Middle),
                "Creator filter coming soon", row);
}

void CloudView::drawStateOverlay(IGraphics& g)
{
  namespace th = ::t3k::theme;
  const IText body(th::kTypeBody, th::kTextMuted, th::kFontBody,
                   EAlign::Center, EVAlign::Middle);
  const IText smallBody(th::kTypeSmall, th::kTextMuted, th::kFontBody,
                        EAlign::Center, EVAlign::Middle);

  switch (mState) {
    case State::SignedOut: {
      const IRECT copy(mBodyRect.L, mBodyRect.MH() - 60.f,
                       mBodyRect.R, mBodyRect.MH() - 20.f);
      g.DrawText(body,
                 "Sign in to TONE3000 to browse the cloud library.",
                 copy);
      break;
    }
    case State::Idle: {
      // Idle is now a transient state — refreshFromSession kicks a
      // trending search immediately on sign-in, so we only see this
      // before the first request fires. A brief "Loading…" is more
      // honest than "Type to search…" because that's what the user
      // is actually waiting on.
      if (mTones.empty()) {
        g.DrawText(body, "Loading\xE2\x80\xA6", mBodyRect);
      }
      break;
    }
    case State::Loading: {
      if (mTones.empty()) {
        g.DrawText(body, "Loading\xE2\x80\xA6", mBodyRect);
      } else {
        // Render in the reserved top banner row — cards start at
        // mBodyRect.T + kStatusBannerH + kBodyPad, so this rect can
        // never be occluded by a card scrolling past the bottom.
        const IRECT banner(mBodyRect.L, mBodyRect.T,
                           mBodyRect.R, mBodyRect.T + kStatusBannerH);
        g.DrawText(smallBody, "Loading more\xE2\x80\xA6", banner);
      }
      break;
    }
    case State::Loaded: {
      if (mTones.empty()) {
        g.DrawText(body, "No tones match these filters.", mBodyRect);
      } else if (!mErrorMessage.empty()) {
        const IRECT banner(mBodyRect.L, mBodyRect.T,
                           mBodyRect.R, mBodyRect.T + kStatusBannerH);
        g.DrawText(smallBody, mErrorMessage.c_str(), banner);
      }
      break;
    }
    case State::Error: {
      const std::string msg = "Error: " + mErrorMessage + " — click to retry";
      g.DrawText(body, msg.c_str(), mBodyRect);
      break;
    }
  }
}

}  // namespace t3k::ui
