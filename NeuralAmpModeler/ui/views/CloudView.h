// CloudView.h — Phase 6 cloud-browser tab.
//
// Composite view sized to the tab body area. Owns:
//   - A header row (T3kSearchBar + sort-cycle T3kButton).
//   - A left sidebar with two T3kAccordion filter groups (Gear, Size)
//     drawing their own checkbox rows inline via drawContent callbacks.
//   - A right card list — T3kCard children stacked vertically with a
//     scroll offset CloudView manages itself (mouse-wheel + mid-drag).
//     Per-tone cards are instantiated lazily; cards whose rect is
//     entirely outside the body get Hide(true)'d so iPlug2 skips
//     their paint while keeping their state.
//   - A re-used T3kSignInPill rendered centered in the body when the
//     user is signed out — catalog endpoints require Bearer auth so
//     there's nothing to browse until sign-in.
//
// Concurrency: all `cloud::Tone3000Client` completion lambdas fire on
// a worker thread. We stash the parsed result under mResultMtx; the
// next paint cycle reads it, allocates cards, and updates UI. State
// outside Draw / OnAttached must therefore take mResultMtx.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "IControl.h"

#include "../../cloud/Tone3000Types.h"
#include "../../cloud/Tone3000Client.h"
#include "../../net/CancellationToken.h"
#include "../controls/T3kCard.h"  // for T3kCard::DownloadState in mDlByToneId

namespace t3k::ui {

class T3kSearchBar;
class T3kButton;
class T3kAccordion;
class T3kSignInPill;
class T3kDetailModal;

class CloudView : public iplug::igraphics::IControl {
public:
  explicit CloudView(const iplug::igraphics::IRECT& bounds);
  ~CloudView() override;

  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnResize() override;
  void OnAttached() override;
  void OnMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) override;
  void OnMouseDrag(float x, float y, float dX, float dY,
                   const iplug::igraphics::IMouseMod& mod) override;
  void OnMouseWheel(float x, float y,
                    const iplug::igraphics::IMouseMod& mod, float d) override;

  // Cascade hide to child controls (iPlug2 attaches flat — see
  // ToneView::Hide for the same pattern).
  void Hide(bool hide) override;

private:
  // ── State machine ──────────────────────────────────────────────
  enum class State {
    SignedOut,  // no Bearer token; render sign-in CTA only
    Idle,       // signed in, search bar empty (no fetch in flight)
    Loading,    // request in flight (search or pagination)
    Loaded,     // result rendered
    Error,      // non-200 / parse failure — error + retry button
  };

  // ── Action handlers ────────────────────────────────────────────
  void onSearchChanged(const std::string& q);
  void onSortClicked();
  void onCardSelected(int toneIndex);
  void onCardDownload(int toneIndex);
  void onCardDetail(int toneIndex);
  void onSignInClicked();
  void toggleGear(::t3k::cloud::Gear g);
  void toggleSize(::t3k::cloud::Size s);

  // Resync mState from cloud::Session and rebuild the view accordingly.
  void refreshFromSession();

  // Kick a fresh search at page 1 (resets mTones, attaches new cards).
  // Cancels any in-flight request first.
  void startSearch();

  // Fetch the next page (mNextPage). Appends results to mTones / mCards.
  void fetchNextPage();

  // After mTones changes, instantiate any missing T3kCard children and
  // update their positions. Idempotent.
  void rebuildCards();

  // Reposition cards based on mScrollOffset. Cards whose Y rect leaves
  // the body area are Hide(true)'d so iPlug2 skips their paint.
  void layoutCards();

  // Apply a wheel delta `d` to mScrollOffset. Shared by OnMouseWheel
  // (cursor over CloudView body padding) and the per-card wheel
  // forwarder (cursor over a T3kCard — iPlug2 dispatches the wheel
  // to the topmost control under the cursor, so cards have to
  // forward explicitly).
  void scrollBy(float d);

  // Pull any worker-thread completion result into UI state. Called
  // from Draw at the start of each paint cycle so the GUI thread owns
  // all card mutations.
  void drainPendingResult();

  // Hit-test against the per-accordion checkbox rows. Returns true iff
  // the click was handled (toggled a filter).
  bool handleSidebarClick(float x, float y);

  // Draw the sidebar checkbox / placeholder content inside `r` for the
  // matching accordion. Gear / Technical (Size) record their checkbox
  // rects into mGearRowRects / mSizeRowRects so handleSidebarClick can
  // hit-test them; Tags / Makes / Creators draw a "coming in Phase 7"
  // placeholder line (the public SDK doesn't yet surface those filter
  // params — see the Phase 6 plan).
  void drawGearAccordion(const iplug::igraphics::IRECT& r);
  void drawTagsAccordion(const iplug::igraphics::IRECT& r);
  void drawMakesAccordion(const iplug::igraphics::IRECT& r);
  void drawCreatorsAccordion(const iplug::igraphics::IRECT& r);
  void drawTechnicalAccordion(const iplug::igraphics::IRECT& r);

  // Recompute Y positions of the 5 sidebar accordions so collapsed
  // ones consume only header height. Called from OnResize and from
  // any accordion's onToggle callback.
  void layoutSidebar();

  // Render the empty / loading / error overlays — only one is non-empty
  // at any given moment.
  void drawStateOverlay(iplug::igraphics::IGraphics& g);

  // ── Layout sub-rects (recomputed in OnResize) ──────────────────
  iplug::igraphics::IRECT mHeaderRect;
  iplug::igraphics::IRECT mSearchRect;
  iplug::igraphics::IRECT mSortRect;
  iplug::igraphics::IRECT mSidebarRect;
  iplug::igraphics::IRECT mBodyRect;

  // Cached rects for the per-checkbox rows in each accordion (filled
  // when the accordion's drawContent fires; consumed by
  // handleSidebarClick).
  std::vector<std::pair<::t3k::cloud::Gear, iplug::igraphics::IRECT>> mGearRowRects;
  std::vector<std::pair<::t3k::cloud::Size, iplug::igraphics::IRECT>> mSizeRowRects;

  // ── Children (owned by IGraphics) ──────────────────────────────
  T3kSearchBar*  mSearchBar   = nullptr;
  T3kButton*     mSortBtn     = nullptr;
  // Five filter accordions matching tone3000.com/search.
  T3kAccordion*  mGearAcc     = nullptr;
  T3kAccordion*  mTagsAcc     = nullptr;
  T3kAccordion*  mMakesAcc    = nullptr;
  T3kAccordion*  mCreatorsAcc = nullptr;
  T3kAccordion*  mTechAcc     = nullptr;  // Size lives inside this one
  T3kSignInPill* mSignInPill  = nullptr;
  T3kDetailModal* mDetailModal = nullptr;
  std::vector<T3kCard*> mCards;

  // ── Data ───────────────────────────────────────────────────────
  State mState = State::Idle;
  // Default sort = Trending so the empty-query landing view mirrors
  // tone3000.com/search ("Trending" by default). startSearch on
  // first sign-in fires immediately with an empty query, populating
  // the right pane with trending tones — same UX as the website.
  ::t3k::cloud::SearchTonesParams mQuery{
      /*query*/      "",
      /*page*/       1,
      /*page_size*/  25,
      /*sort*/       ::t3k::cloud::TonesSort::Trending,
      /*gears*/      {},
      /*sizes*/      {},
  };
  std::set<::t3k::cloud::Gear> mSelectedGears;
  std::set<::t3k::cloud::Size> mSelectedSizes;
  std::vector<::t3k::cloud::Tone> mTones;
  int  mNextPage  = 1;       // next page to fetch (1 = first page)
  int  mTotalPages = 0;
  bool mHasMore   = false;
  std::string mErrorMessage;
  float mScrollOffset = 0.f;

  // ── Async plumbing ─────────────────────────────────────────────
  // Worker-thread side: completion lambdas stash the parsed result
  // here and toggle mResultReady. Draw() picks it up under the mutex
  // and applies the change. mPendingToken cancels any in-flight
  // request when a new search starts.
  std::mutex mResultMtx;
  std::atomic<bool> mResultReady{false};
  ::t3k::cloud::ToneSearchResult mPendingResult;
  bool mPendingResultIsAppend = false;  // false = first page, true = next page
  ::t3k::net::CancellationToken mPendingToken;

  // Session listener id — unsubscribed in dtor.
  int mSessionListenerId = 0;

  // Downloader subscription. Set in OnAttached, unsubscribed in
  // dtor. The listener stashes the latest status under mDlStatusMtx
  // and flips dirty; Draw drains it into the matching card via
  // setDownloadState (and into mErrorMessage for the top-of-body
  // status banner).
  int                       mDlListenerId = 0;
  mutable std::mutex        mDlStatusMtx;
  std::string               mDlStatusText;          // top-of-body banner text
  std::chrono::steady_clock::time_point mDlStatusExpiry{};

  // Per-tone download state captured by the Downloader listener.
  // Drained in Draw() by pushing into the matching T3kCard's pill.
  // tone_id is the key because cards rebuild on every fresh search
  // (mCards array indices change) but tone ids stay stable across
  // searches if the same tone reappears.
  struct ToneDlState {
    T3kCard::DownloadState state = T3kCard::DownloadState::Idle;
    std::string            label;
  };
  std::unordered_map<int, ToneDlState> mDlByToneId;  // under mDlStatusMtx

  // tone_id → index into mCards / mTones. Rebuilt in rebuildCards.
  // Used to look up the right card when a Downloader status event
  // fires (the event carries tone_id, not the card index).
  std::unordered_map<int, std::size_t> mToneIdToCardIdx;
};

}  // namespace t3k::ui
