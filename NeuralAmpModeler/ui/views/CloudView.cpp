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
  d.image        = std::nullopt;  // Phase 7 wires thumbnail download
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
  // Cancel anything in flight so the worker's completion can't fire
  // into a destroyed `this`.
  mPendingToken.cancel();
}

void CloudView::Hide(bool hide)
{
  IControl::Hide(hide);
  if (mSearchBar)  mSearchBar->Hide(hide);
  if (mSortBtn)    mSortBtn  ->Hide(hide);
  if (mGearAcc)    mGearAcc  ->Hide(hide);
  if (mSizeAcc)    mSizeAcc  ->Hide(hide);
  if (mSignInPill) mSignInPill->Hide(hide || mState != State::SignedOut);
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

  mGearAccordionRect = IRECT(mSidebarRect.L + kSidebarPad,
                              mSidebarRect.T + kSidebarPad,
                              mSidebarRect.R - kSidebarPad,
                              mSidebarRect.T + kSidebarPad + kAccordionH);
  mSizeAccordionRect = IRECT(mGearAccordionRect.L,
                              mGearAccordionRect.B + th::kS2,
                              mGearAccordionRect.R,
                              mGearAccordionRect.B + th::kS2 + kAccordionH);

  if (mSearchBar) mSearchBar->SetTargetAndDrawRECTs(mSearchRect);
  if (mSortBtn)   mSortBtn  ->SetTargetAndDrawRECTs(mSortRect);
  if (mGearAcc)   mGearAcc  ->SetTargetAndDrawRECTs(mGearAccordionRect);
  if (mSizeAcc)   mSizeAcc  ->SetTargetAndDrawRECTs(mSizeAccordionRect);

  if (mSignInPill) {
    const float pillW = 140.f, pillH = 32.f;
    const float cx = mBodyRect.MW(), cy = mBodyRect.MH() + 30.f;
    mSignInPill->SetTargetAndDrawRECTs(
        IRECT(cx - pillW * 0.5f, cy - pillH * 0.5f,
              cx + pillW * 0.5f, cy + pillH * 0.5f));
  }

  layoutCards();
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

  mGearAcc = new T3kAccordion(mGearAccordionRect,
      "Gear",
      /*measureContentHeight*/ []() { return 5 * kCheckboxRowH + 8.f; },
      /*drawContent*/ [this](const IRECT& r) { this->drawGearAccordion(r); },
      /*initiallyOpen*/ true);
  g->AttachControl(mGearAcc);

  mSizeAcc = new T3kAccordion(mSizeAccordionRect,
      "Size",
      /*measureContentHeight*/ []() { return 5 * kCheckboxRowH + 8.f; },
      /*drawContent*/ [this](const IRECT& r) { this->drawSizeAccordion(r); },
      /*initiallyOpen*/ true);
  g->AttachControl(mSizeAcc);

  mSignInPill = new T3kSignInPill(
      IRECT(0.f, 0.f, 1.f, 1.f),
      /*onClick*/ [this] { this->onSignInClicked(); });
  g->AttachControl(mSignInPill);

  OnResize();

  // Session listener — flip the empty/loaded state when the user
  // signs in or out mid-session. Body of the callback just marks
  // dirty; the next Draw cycle reads Session::state() under its own
  // mutex.
  mSessionListenerId = ::t3k::cloud::Session::instance().subscribe(
      [this](const ::t3k::cloud::SessionEvent& /*ev*/) {
        SetDirty(false);
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
  if (mSignInPill) mSignInPill->Hide(mState != State::SignedOut);
  if (mGearAcc)    mGearAcc->Hide(mState == State::SignedOut);
  if (mSizeAcc)    mSizeAcc->Hide(mState == State::SignedOut);

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
  mState = signedIn ? State::Idle : State::SignedOut;
  if (signedIn && !mTones.empty()) mState = State::Loaded;
  SetDirty(false);
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

void CloudView::onCardDownload(int /*toneIndex*/)
{
  mErrorMessage = "Phase 7 will deliver real downloads.";
  // Keep mState == Loaded so cards stay visible — the message renders
  // as a small banner at the bottom of the body (see drawStateOverlay).
  SetDirty(false);
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
    g->AttachControl(c);
    mCards.push_back(c);
  }

  // If mTones shrunk, hide the surplus IControls but keep them around —
  // RemoveControl frees, and rebuilding cards on every fresh search
  // would churn iPlug2's control list.
  for (size_t i = mTones.size(); i < mCards.size(); ++i) {
    if (mCards[i]) mCards[i]->Hide(true);
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

    const float top = mBodyRect.T + kBodyPad
                      + static_cast<float>(i) * (kCardH + kCardGapY)
                      - mScrollOffset;
    const float bot = top + kCardH;
    const bool offscreen = (bot < mBodyRect.T) || (top > mBodyRect.B);
    c->SetTargetAndDrawRECTs(IRECT(bodyLeft, top, bodyRight, bot));
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

void CloudView::drawSizeAccordion(const IRECT& r)
{
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
      if (mTones.empty()) {
        g.DrawText(body,
                   "Type to search the TONE3000 catalog\xE2\x80\xA6",
                   mBodyRect);
      }
      break;
    }
    case State::Loading: {
      if (mTones.empty()) {
        g.DrawText(body, "Loading\xE2\x80\xA6", mBodyRect);
      } else {
        const IRECT bottom(mBodyRect.L, mBodyRect.B - 22.f,
                           mBodyRect.R, mBodyRect.B);
        g.DrawText(smallBody, "Loading more\xE2\x80\xA6", bottom);
      }
      break;
    }
    case State::Loaded: {
      if (mTones.empty()) {
        g.DrawText(body, "No tones match these filters.", mBodyRect);
      } else if (!mErrorMessage.empty()) {
        const IRECT bottom(mBodyRect.L, mBodyRect.B - 22.f,
                           mBodyRect.R, mBodyRect.B);
        g.DrawText(smallBody, mErrorMessage.c_str(), bottom);
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
