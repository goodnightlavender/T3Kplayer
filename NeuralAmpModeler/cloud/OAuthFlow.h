// OAuthFlow.h — orchestrates the TONE3000 PKCE sign-in handshake
// (Phase 5).
//
// signIn() is a fire-and-forget call that runs the entire flow on a
// background std::thread:
//   1. Generate PKCE verifier + S256 challenge + state nonce.
//   2. Start the LoopbackServer.
//   3. Build the authorize URL and ShellExecute() the user's default
//      browser to it.
//   4. Block on awaitCallback(5 min) for the browser to hit
//      `http://127.0.0.1:53000/callback?code=...&state=...`.
//   5. Validate the returned state matches our nonce.
//   6. POST the code (+ verifier + redirect_uri + client_id) to the
//      token endpoint via the Phase 4 net::HttpClient.
//   7. Parse the JSON response into AuthResult.
//   8. Fire the completion lambda.
//
// The completion runs on the worker thread — UI consumers must marshal
// back to the GUI thread themselves (Session does this by storing the
// result and letting the next paint cycle pick it up).
//
// isConfigured() is a cheap strcmp check against the placeholder
// kClientId. UI code uses it to gate the "Sign in" button — when
// false, clicking surfaces a toast directing the user to OAuthConfig.h
// instead of starting the flow.

#pragma once

#include <functional>
#include <string>

namespace t3k::cloud {

struct AuthResult {
  bool success = false;
  std::string access_token;
  std::string refresh_token;
  int         expires_in = 0;    // seconds until access_token expiry
  std::string error_message;
};

class OAuthFlow {
public:
  using Completion = std::function<void(const AuthResult&)>;

  // Returns true if kClientId has been replaced with a real app id.
  static bool isConfigured();

  // Run the PKCE flow on a background thread; fires `onDone` when the
  // flow terminates (success, error, timeout, or cancel). Safe to
  // call from any thread.
  static void signIn(Completion onDone);
};

}  // namespace t3k::cloud
