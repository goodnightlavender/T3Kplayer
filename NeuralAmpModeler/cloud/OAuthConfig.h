// OAuthConfig.h — build-time constants for the TONE3000 OAuth 2.0 +
// PKCE flow (Phase 5).
//
// kClientId is intentionally a placeholder ("REPLACE_ME") in the
// open-source fork. To enable real sign-in:
//   1. Register an OAuth app at tone3000.com → Settings → API Keys.
//   2. Add `http://127.0.0.1:53000/callback` as a permitted redirect
//      URI on that app.
//   3. Paste the issued client_id into kClientId below.
//   4. Rebuild.
//
// Until kClientId is replaced, `OAuthFlow::isConfigured()` returns
// false and clicking "Sign in" surfaces a toast directing the user to
// this file.
//
// The Debug build also exposes a "Dev: mock sign in" entry in the
// avatar dropdown (gated behind T3K_DEV_MOCK_OAUTH) so the full UI +
// DPAPI persistence cycle is exercisable without a registered app.

#pragma once

namespace t3k::cloud::oauth {

constexpr const char* kClientId         = "REPLACE_ME";
constexpr const char* kAuthorizeUrl     = "https://tone3000.com/api/v1/oauth/authorize";
constexpr const char* kTokenUrl         = "https://tone3000.com/api/v1/oauth/token";
constexpr const char* kUserUrl          = "https://tone3000.com/api/v1/user";
constexpr const char* kRedirectUri      = "http://127.0.0.1:53000/callback";
constexpr int         kPortStart        = 53000;
constexpr int         kPortRange        = 10;
constexpr int         kCallbackTimeoutMs = 5 * 60 * 1000;  // 5 minutes

}  // namespace t3k::cloud::oauth
