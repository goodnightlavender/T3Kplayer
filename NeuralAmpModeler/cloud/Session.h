// Session.h — Meyers-singleton lifecycle owner for the signed-in
// TONE3000 user state (Phase 5).
//
// State machine:
//                          ┌────────────┐
//                          │ SignedOut  │ ◀──────────────────────┐
//                          └─────┬──────┘                          │
//                       signIn() │                                 │
//                                ▼                                 │
//                          ┌────────────┐                          │
//                          │ SigningIn  │── OAuth fails ───────────┤
//                          └─────┬──────┘                          │
//                                │ OAuth success                   │
//                                ▼                                 │
//                          ┌────────────┐                          │
//                          │ SignedIn   │── signOut() ─────────────┤
//                          └─────┬──────┘                          │
//                                │ near-expiry tick                │
//                                ▼                                 │
//                          ┌────────────┐                          │
//                          │ Refreshing │── refresh fails ─────────┘
//                          └─────┬──────┘
//                                │ refresh success
//                                ▼
//                          (back to SignedIn)
//
// Lifecycle:
//   - Constructor (called by `Session::instance()` on first use):
//     calls TokenStore::loadRefreshToken(). If present, transitions to
//     `Refreshing` and calls `attemptRefresh()` to mint a fresh access
//     token. On success → SignedIn; on failure → SignedOut + wipe.
//   - Spawns a dedicated refresh thread that loops `wait_for(60s)` on
//     a condition variable, checking expiry and calling
//     attemptRefresh() ~60s before the access token expires.
//   - Destructor sets shutdown flag, notifies the cv, joins the
//     thread.
//
// Mock path (`mockSignIn()` — gated behind T3K_DEV_MOCK_OAUTH at the
// call site): builds a synthetic User, stores `"MOCK-<random>"` as the
// refresh token via TokenStore. On the next plug-in load,
// `attemptRefresh` detects the `MOCK-` prefix and short-circuits
// without a network call.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "SessionEvent.h"
#include "User.h"

namespace t3k::cloud {

class Session {
public:
  enum class State { SignedOut, SigningIn, SignedIn, Refreshing };

  static Session& instance();

  State                       state() const;
  std::optional<User>         currentUser() const;
  std::optional<std::string>  accessTokenIfValid() const;

  // Start the real OAuth flow on a background thread (no-op if the
  // session is currently SigningIn or SignedIn).
  void signIn();

  // Forget all credentials, wipe DPAPI, emit SignedOut.
  void signOut();

  // Mock sign-in: synthesizes a test user and a MOCK-prefixed refresh
  // token. Persists via TokenStore so plug-in reload still appears
  // signed-in.
  void mockSignIn();

  using Listener = std::function<void(const SessionEvent&)>;

  // Returns a non-zero subscription token. Listener fires on whichever
  // thread published the event (typically the GUI thread for state
  // transitions; the background OAuth thread for SignInFailed when
  // the user aborts the browser flow).
  int  subscribe(Listener cb);
  void unsubscribe(int id);

private:
  Session();
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // Publish to all subscribers under a snapshot of the listener list
  // (so listeners can safely subscribe/unsubscribe from inside their
  // callback).
  void publish(const SessionEvent& ev);

  // Run the refresh flow synchronously on whichever thread calls it.
  // On success: updates mAccessToken/mAccessExpiryMs/mUser, sets state
  // to SignedIn, publishes SignedIn. On failure: wipes credentials,
  // sets state to SignedOut, publishes SessionExpired.
  void attemptRefresh(const std::string& refreshToken);

  // Background-thread entry point. Loops on mCv with a 60-second
  // timeout; on each wake checks if mAccessExpiryMs is within 60s of
  // `now` and triggers a refresh.
  void refreshLoop();

  // Build the synthetic mock user.
  static User makeMockUser();

  mutable std::mutex   mMtx;
  std::atomic<State>   mState{State::SignedOut};
  std::optional<User>  mUser;
  std::string          mAccessToken;
  int64_t              mAccessExpiryMs = 0;   // millis since epoch
  std::string          mRefreshToken;         // kept in-memory between refreshes
  std::vector<std::pair<int, Listener>> mListeners;
  int                  mNextListenerId = 1;

  // Refresh thread + shutdown signal.
  std::atomic<bool>        mShutdown{false};
  std::condition_variable  mCv;
  std::thread              mRefreshThread;
};

}  // namespace t3k::cloud
