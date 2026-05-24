// OAuthConfig.h — build-time constants for the TONE3000 OAuth 2.0 +
// PKCE flow (Phase 5).
//
// kClientId is the PUBLIC half of an OAuth app registered on
// tone3000.com → Settings → API Keys. It's safe to ship in the binary
// (publishable, like a Stripe publishable key) because PKCE — RFC 7636
// — replaces the client_secret with a per-flow code_verifier. The
// secret half (`t3k_cs_…`) is for SERVER-SIDE use only (e.g. the
// CloudFlare Worker in Phase 8) and MUST NEVER be embedded here, in
// any source file, or committed to git.
//
// To enable real sign-in for the open-source fork:
//   1. Register an OAuth app at tone3000.com → Settings → API Keys.
//   2. Add `http://127.0.0.1:53000/callback` as a permitted redirect
//      URI on that app.
//   3. Paste the issued public client_id into kClientId below.
//   4. Rebuild.
//
// Until kClientId is the canonical placeholder, `OAuthFlow::isConfigured()`
// returns false; clicking "Sign in" surfaces a toast directing the user
// to this file, and the avatar dropdown exposes a "Dev: mock sign in"
// entry so the UI + DPAPI persistence cycle is exercisable without a
// registered app.

#pragma once

namespace t3k::cloud::oauth {

// Public OAuth client_id registered for "TONE3000 Player" on tone3000.com.
constexpr const char* kClientId         = "t3k_pub_fYpfedBsFbpRlmA-nTIqXLb3iTkTCLF1";
constexpr const char* kAuthorizeUrl     = "https://tone3000.com/api/v1/oauth/authorize";
constexpr const char* kTokenUrl         = "https://tone3000.com/api/v1/oauth/token";
constexpr const char* kUserUrl          = "https://tone3000.com/api/v1/user";
constexpr const char* kRedirectUri      = "http://127.0.0.1:53000/callback";
constexpr int         kPortStart        = 53000;
constexpr int         kPortRange        = 10;
constexpr int         kCallbackTimeoutMs = 5 * 60 * 1000;  // 5 minutes

}  // namespace t3k::cloud::oauth
