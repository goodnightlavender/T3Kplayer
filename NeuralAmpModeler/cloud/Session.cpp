// Session.cpp — auth state machine implementation. See Session.h.
//
// Implementation notes:
//   - The constructor reads tokens.dpapi via TokenStore. If a token is
//     present, transitions through SigningIn -> attemptRefresh ->
//     SignedIn (or SignedOut on failure). The refresh thread is
//     started unconditionally — even when SignedOut, the thread just
//     sleeps on the cv until a signIn() event wakes it. (We notify
//     the cv from signIn / mockSignIn / signOut so the loop re-checks
//     state promptly.)
//   - publish() snapshots the listener list under mMtx, then releases
//     the lock before invoking each listener. This is the same pattern
//     as library::EventBus — listeners can subscribe/unsubscribe from
//     inside their own callback without deadlocking.
//   - Real refresh path POSTs `grant_type=refresh_token&refresh_token=
//     <token>&client_id=<id>` to kTokenUrl via the synchronous
//     PostFormSync helper (same pattern as OAuthFlow). Mock path
//     ("MOCK-" prefix) short-circuits without HTTP.
//   - "Near expiry" is 60s before mAccessExpiryMs — same as the spec's
//     "proactive refresh ~60s before expiry."

#include "Session.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "nlohmann/json.hpp"

#include "Crypto.h"
#include "OAuthConfig.h"
#include "OAuthFlow.h"
#include "TokenStore.h"
#include "../net/HttpClient.h"
#include "../net/HttpRequest.h"
#include "../net/HttpResponse.h"
#include "../net/UrlEncode.h"

namespace t3k::cloud {

namespace {

using json = nlohmann::json;

constexpr const char* kMockPrefix = "MOCK-";

int64_t NowMs()
{
  using namespace std::chrono;
  return duration_cast<milliseconds>(
      system_clock::now().time_since_epoch()).count();
}

// Same synchronous POST helper as OAuthFlow.cpp — we keep a copy here
// rather than expose a shared module so Session doesn't gain a
// surface-level dependency on OAuthFlow's internals.
::t3k::net::HttpResponse PostFormSync(const std::string& url,
                                      const std::string& body)
{
  ::t3k::net::HttpRequest req;
  req.method = ::t3k::net::HttpMethod::Post;
  req.url    = url;
  req.headers["Content-Type"] = "application/x-www-form-urlencoded";
  req.headers["Accept"]       = "application/json";
  req.body.assign(body.begin(), body.end());

  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  ::t3k::net::HttpResponse out;

  ::t3k::net::HttpClient::instance().send(
      std::move(req),
      [&](const ::t3k::net::HttpResponse& res) {
        std::lock_guard<std::mutex> lk(m);
        out = res;
        done = true;
        cv.notify_one();
      });

  std::unique_lock<std::mutex> lk(m);
  cv.wait(lk, [&] { return done; });
  return out;
}

}  // namespace

Session& Session::instance()
{
  static Session s;
  return s;
}

Session::Session()
{
  // Try to restore a previous session from DPAPI. The lookup may also
  // be the synthetic mock token (`MOCK-...`) — attemptRefresh handles
  // both paths.
  auto refresh = TokenStore::loadRefreshToken();
  if (refresh.has_value() && !refresh->empty()) {
    mState = State::Refreshing;
    attemptRefresh(*refresh);
  }
  // Start the refresh worker unconditionally so signIn / mockSignIn
  // can wake it later.
  mRefreshThread = std::thread([this]() { refreshLoop(); });
}

Session::~Session()
{
  mShutdown = true;
  mCv.notify_all();
  if (mRefreshThread.joinable()) mRefreshThread.join();
}

Session::State Session::state() const
{
  return mState.load();
}

std::optional<User> Session::currentUser() const
{
  std::lock_guard<std::mutex> lk(mMtx);
  return mUser;
}

std::optional<std::string> Session::accessTokenIfValid() const
{
  std::lock_guard<std::mutex> lk(mMtx);
  if (mAccessToken.empty()) return std::nullopt;
  if (mAccessExpiryMs > 0 && NowMs() >= mAccessExpiryMs) return std::nullopt;
  return mAccessToken;
}

void Session::signIn()
{
  // No-op if we're already in flight or signed in.
  const auto s = mState.load();
  if (s == State::SigningIn || s == State::SignedIn ||
      s == State::Refreshing) {
    return;
  }
  mState = State::SigningIn;
  publish({ SessionEvent::Kind::SignInStarted, std::nullopt, "" });

  OAuthFlow::signIn([this](const AuthResult& r) {
    if (!r.success) {
      mState = State::SignedOut;
      SessionEvent ev;
      ev.kind = SessionEvent::Kind::SignInFailed;
      ev.error_message = r.error_message;
      publish(ev);
      return;
    }

    // Stash the new credentials.
    {
      std::lock_guard<std::mutex> lk(mMtx);
      mAccessToken = r.access_token;
      mRefreshToken = r.refresh_token;
      mAccessExpiryMs = (r.expires_in > 0)
          ? NowMs() + int64_t(r.expires_in) * 1000
          : 0;
      // The real OAuth flow doesn't fetch /api/v1/user in Phase 5
      // (that lands in Phase 6). Leave mUser empty for now — the UI
      // shows "Signed in" without a username until then.
      mUser.reset();
    }

    if (!r.refresh_token.empty()) {
      TokenStore::saveRefreshToken(r.refresh_token);
    }

    mState = State::SignedIn;
    SessionEvent ev;
    ev.kind = SessionEvent::Kind::SignedIn;
    ev.user = std::nullopt;  // username comes from Phase 6
    publish(ev);
    fetchCurrentUserAsync();
    mCv.notify_all();  // wake the refresh thread to pick up new expiry
  });
}

void Session::signOut()
{
  TokenStore::clearRefreshToken();
  {
    std::lock_guard<std::mutex> lk(mMtx);
    mUser.reset();
    mAccessToken.clear();
    mRefreshToken.clear();
    mAccessExpiryMs = 0;
  }
  mState = State::SignedOut;
  publish({ SessionEvent::Kind::SignedOut, std::nullopt, "" });
  mCv.notify_all();
}

void Session::mockSignIn()
{
  const User u = makeMockUser();

  // Build a MOCK-prefixed refresh token. The next plug-in load will
  // see this and short-circuit attemptRefresh.
  const auto rand = randomBytes(16);
  std::string fake = std::string(kMockPrefix);
  if (!rand.empty()) {
    fake += base64UrlEncode(rand.data(), rand.size(), /*withPadding*/ false);
  } else {
    fake += "deadbeef";
  }

  {
    std::lock_guard<std::mutex> lk(mMtx);
    mUser = u;
    mAccessToken = "MOCK-ACCESS";
    mRefreshToken = fake;
    mAccessExpiryMs = NowMs() + int64_t(60 * 60) * 1000;  // 1h
  }
  TokenStore::saveRefreshToken(fake);

  mState = State::SignedIn;
  SessionEvent ev;
  ev.kind = SessionEvent::Kind::SignedIn;
  ev.user = u;
  publish(ev);
  mCv.notify_all();
}

void Session::fetchCurrentUserAsync()
{
  auto token = accessTokenIfValid();
  if (!token.has_value() || token->rfind("MOCK-", 0) == 0) return;

  ::t3k::net::HttpRequest req;
  req.method = ::t3k::net::HttpMethod::Get;
  req.url = ::t3k::cloud::oauth::kUserUrl;
  req.headers["Authorization"] = "Bearer " + *token;
  req.headers["Accept"] = "application/json";
  req.headers["User-Agent"] = "TONE3000Player/0.1";
  req.timeout_ms = 15'000;

  ::t3k::net::HttpClient::instance().send(std::move(req),
      [this](const ::t3k::net::HttpResponse& res) {
        if (res.status_code < 200 || res.status_code >= 300 || res.body.empty()) return;
        try {
          const std::string body(res.body.begin(), res.body.end());
          const json j = json::parse(body);
          User u;
          u.id = j.value("id", std::string{});
          u.username = j.value("username", std::string{});
          u.display_name = j.value("display_name", std::string{});
          u.email = j.value("email", std::string{});
          u.avatar_url = j.value("avatar_url", std::string{});
          if (u.avatar_url.empty()) u.avatar_url = j.value("avatar", std::string{});
          {
            std::lock_guard<std::mutex> lk(mMtx);
            mUser = u;
          }
          SessionEvent ev;
          ev.kind = SessionEvent::Kind::SignedIn;
          ev.user = u;
          publish(ev);
        } catch (...) {
        }
      });
}

int Session::subscribe(Listener cb)
{
  std::lock_guard<std::mutex> lk(mMtx);
  const int id = mNextListenerId++;
  mListeners.emplace_back(id, std::move(cb));
  return id;
}

void Session::unsubscribe(int id)
{
  if (id <= 0) return;
  std::lock_guard<std::mutex> lk(mMtx);
  for (auto it = mListeners.begin(); it != mListeners.end(); ++it) {
    if (it->first == id) {
      mListeners.erase(it);
      return;
    }
  }
}

void Session::publish(const SessionEvent& ev)
{
  std::vector<Listener> snapshot;
  {
    std::lock_guard<std::mutex> lk(mMtx);
    snapshot.reserve(mListeners.size());
    for (const auto& p : mListeners) snapshot.push_back(p.second);
  }
  for (const auto& l : snapshot) {
    if (l) l(ev);
  }
}

void Session::attemptRefresh(const std::string& refreshToken)
{
  // ── Mock path ─────────────────────────────────────────────
  if (refreshToken.size() >= std::strlen(kMockPrefix) &&
      std::memcmp(refreshToken.data(), kMockPrefix,
                  std::strlen(kMockPrefix)) == 0) {
    const User u = makeMockUser();
    {
      std::lock_guard<std::mutex> lk(mMtx);
      mUser = u;
      mAccessToken = "MOCK-ACCESS";
      mRefreshToken = refreshToken;
      mAccessExpiryMs = NowMs() + int64_t(60 * 60) * 1000;
    }
    mState = State::SignedIn;
    SessionEvent ev;
    ev.kind = SessionEvent::Kind::SignedIn;
    ev.user = u;
    publish(ev);
    return;
  }

  // ── Real path ─────────────────────────────────────────────
  if (!OAuthFlow::isConfigured()) {
    // Can't refresh against unconfigured OAuth; treat as expired.
    TokenStore::clearRefreshToken();
    {
      std::lock_guard<std::mutex> lk(mMtx);
      mUser.reset();
      mAccessToken.clear();
      mRefreshToken.clear();
      mAccessExpiryMs = 0;
    }
    mState = State::SignedOut;
    publish({ SessionEvent::Kind::SessionExpired, std::nullopt,
              "OAuth not configured; cleared stale refresh token" });
    return;
  }

  std::map<std::string, std::string> form;
  form["grant_type"]    = "refresh_token";
  form["refresh_token"] = refreshToken;
  form["client_id"]     = ::t3k::cloud::oauth::kClientId;
  const std::string body = ::t3k::net::urlEncodeQueryString(form);

  auto res = PostFormSync(::t3k::cloud::oauth::kTokenUrl, body);
  if (res.status_code < 200 || res.status_code >= 300) {
    // Refresh failed — wipe and emit SessionExpired.
    TokenStore::clearRefreshToken();
    {
      std::lock_guard<std::mutex> lk(mMtx);
      mUser.reset();
      mAccessToken.clear();
      mRefreshToken.clear();
      mAccessExpiryMs = 0;
    }
    mState = State::SignedOut;
    publish({ SessionEvent::Kind::SessionExpired, std::nullopt,
              "Refresh failed: HTTP " + std::to_string(res.status_code) });
    return;
  }

  try {
    const std::string bodyStr(res.body.begin(), res.body.end());
    const json j = json::parse(bodyStr);
    std::string newAccess  = j.value("access_token", std::string{});
    std::string newRefresh = j.value("refresh_token", refreshToken);
    const int   expiresIn  = j.value("expires_in", 0);
    if (newAccess.empty()) {
      throw std::runtime_error("missing access_token");
    }
    {
      std::lock_guard<std::mutex> lk(mMtx);
      mAccessToken    = std::move(newAccess);
      mRefreshToken   = newRefresh;
      mAccessExpiryMs = (expiresIn > 0)
          ? NowMs() + int64_t(expiresIn) * 1000
          : 0;
    }
    if (!newRefresh.empty() && newRefresh != refreshToken) {
      TokenStore::saveRefreshToken(newRefresh);
    }
    mState = State::SignedIn;
    SessionEvent ev;
    ev.kind = SessionEvent::Kind::SignedIn;
    ev.user = std::nullopt;
    publish(ev);
    fetchCurrentUserAsync();
  } catch (const std::exception& e) {
    TokenStore::clearRefreshToken();
    {
      std::lock_guard<std::mutex> lk(mMtx);
      mUser.reset();
      mAccessToken.clear();
      mRefreshToken.clear();
      mAccessExpiryMs = 0;
    }
    mState = State::SignedOut;
    publish({ SessionEvent::Kind::SessionExpired, std::nullopt,
              std::string("Refresh parse failed: ") + e.what() });
  }
}

void Session::refreshLoop()
{
  std::unique_lock<std::mutex> lk(mMtx);
  while (!mShutdown.load()) {
    // Wake every 60s OR on notify (signIn / signOut / mockSignIn).
    mCv.wait_for(lk, std::chrono::seconds(60),
                 [this] { return mShutdown.load(); });
    if (mShutdown.load()) break;

    // Only attempt refresh when SignedIn and access token is within
    // 60s of expiry.
    if (mState.load() != State::SignedIn) continue;
    if (mAccessExpiryMs <= 0) continue;
    const int64_t now = NowMs();
    if (now < mAccessExpiryMs - 60 * 1000) continue;

    // Snapshot the refresh token then drop the lock — attemptRefresh
    // re-acquires it internally.
    const std::string token = mRefreshToken;
    mState = State::Refreshing;
    lk.unlock();
    attemptRefresh(token);
    lk.lock();
  }
}

User Session::makeMockUser()
{
  User u;
  u.id           = "u_test";
  u.username     = "testuser";
  u.display_name = "Test User";
  u.email        = "test@tone3000.com";
  u.avatar_url   = "";
  return u;
}

}  // namespace t3k::cloud
