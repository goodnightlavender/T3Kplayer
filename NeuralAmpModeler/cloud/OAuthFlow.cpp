// OAuthFlow.cpp — PKCE handshake implementation. See OAuthFlow.h.
//
// Threading: signIn() spawns a detached std::thread. The thread runs
// the full flow synchronously — LoopbackServer + ShellExecute +
// awaitCallback + net::HttpClient POST + JSON parse — then invokes the
// completion lambda before exiting. The thread is detached because the
// caller (Session::signIn) has no way to join it; the LoopbackServer
// destructor runs at thread exit and cleans up the listening socket.
//
// Error model: any failure populates AuthResult.error_message and
// leaves success=false. The completion is always called exactly once.
//
// JSON: nlohmann/json is already vendored under
// iPlug2/Dependencies/Extras/nlohmann/json.hpp (per Phase 3's
// discovery). Use the same include path as ModelSidecar / PresetStore.

#include "OAuthFlow.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "nlohmann/json.hpp"

#include "Crypto.h"
#include "LoopbackServer.h"
#include "OAuthConfig.h"
#include "../net/HttpClient.h"
#include "../net/HttpRequest.h"
#include "../net/HttpResponse.h"
#include "../net/UrlEncode.h"

namespace t3k::cloud {

namespace {

using json = nlohmann::json;

// UTF-8 -> UTF-16 for ShellExecuteW.
std::wstring Widen(const std::string& s)
{
  if (s.empty()) return {};
  const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                      -1, nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<size_t>(n), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1,
                        out.data(), n);
  if (!out.empty() && out.back() == L'\0') out.pop_back();
  return out;
}

// Build the authorize URL with all required PKCE query params.
std::string BuildAuthorizeUrl(const std::string& challenge,
                              const std::string& state,
                              const std::string& redirectUri)
{
  using namespace ::t3k::cloud::oauth;
  std::map<std::string, std::string> q;
  q["response_type"]         = "code";
  q["client_id"]             = kClientId;
  q["redirect_uri"]          = redirectUri;
  q["code_challenge"]        = challenge;
  q["code_challenge_method"] = "S256";
  q["state"]                 = state;
  return std::string(kAuthorizeUrl) + "?" +
         ::t3k::net::urlEncodeQueryString(q);
}

// Build the redirect URI for the LoopbackServer's actual port (might
// be 53001-53009 if 53000 was taken).
std::string BuildRedirectForPort(int port)
{
  // The TONE3000 OAuth app is registered against the exact string
  // `http://127.0.0.1:53000/callback`. If we landed on a different
  // port (53001-53009 fallback), the redirect URI won't match what's
  // registered on the server side — but we still send the actual
  // listening port so the user can complete the flow during local
  // testing where the app is registered with a wildcard or the user
  // updated the redirect URI list. (Production tone3000.com would
  // reject the mismatch.)
  if (port == 53000) return ::t3k::cloud::oauth::kRedirectUri;
  char buf[64];
  std::snprintf(buf, sizeof(buf),
                "http://127.0.0.1:%d/callback", port);
  return std::string(buf);
}

// POST `form` to `url`. Synchronous — blocks on a condition variable
// until the worker-pool completion fires.
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
        out  = res;
        done = true;
        cv.notify_one();
      });

  std::unique_lock<std::mutex> lk(m);
  cv.wait(lk, [&] { return done; });
  return out;
}

}  // namespace

bool OAuthFlow::isConfigured()
{
  // strcmp instead of std::string compare — kClientId is a compile-
  // time string literal and we don't want the std::string ctor cost
  // on the hot path of every UI paint that checks the auth state.
  return std::strcmp(::t3k::cloud::oauth::kClientId, "REPLACE_ME") != 0;
}

void OAuthFlow::signIn(Completion onDone)
{
  // Capture the completion by value and run the entire flow on a
  // detached background thread.
  std::thread([done = std::move(onDone)]() {
    AuthResult result;

    if (!OAuthFlow::isConfigured()) {
      result.success = false;
      result.error_message =
          "OAuth client_id not configured. "
          "Edit cloud/OAuthConfig.h and rebuild.";
      if (done) done(result);
      return;
    }

    // ── PKCE material ─────────────────────────────────────────
    const std::string verifier  = makePkceVerifier();
    const std::string challenge = makePkceChallenge(verifier);
    const std::string state     = makeStateNonce();
    if (verifier.empty() || challenge.empty() || state.empty()) {
      result.success = false;
      result.error_message =
          "PKCE material generation failed (BCrypt unavailable?)";
      if (done) done(result);
      return;
    }

    // ── LoopbackServer ────────────────────────────────────────
    LoopbackServer server;
    if (!server.start(::t3k::cloud::oauth::kPortStart)) {
      result.success = false;
      result.error_message =
          "Failed to bind loopback socket on ports 53000-53009";
      if (done) done(result);
      return;
    }
    const std::string redirectUri = BuildRedirectForPort(server.port());

    // ── Launch the system browser ─────────────────────────────
    const std::string url = BuildAuthorizeUrl(challenge, state, redirectUri);
    const std::wstring urlW = Widen(url);
    const HINSTANCE rc = ::ShellExecuteW(
        nullptr, L"open", urlW.c_str(), nullptr, nullptr, SW_SHOW);
    if (reinterpret_cast<INT_PTR>(rc) <= 32) {
      server.stop();
      result.success = false;
      result.error_message =
          "ShellExecute failed to launch the system browser";
      if (done) done(result);
      return;
    }

    // ── Await the callback ────────────────────────────────────
    LoopbackResult cb = server.awaitCallback(
        ::t3k::cloud::oauth::kCallbackTimeoutMs);
    if (!cb.success) {
      result.success = false;
      result.error_message = cb.error_message.empty()
          ? "OAuth callback failed"
          : cb.error_message;
      if (done) done(result);
      return;
    }

    // ── Validate state nonce ──────────────────────────────────
    auto stateIt = cb.queryParams.find("state");
    if (stateIt == cb.queryParams.end() || stateIt->second != state) {
      result.success = false;
      result.error_message = "OAuth state nonce mismatch (possible CSRF)";
      if (done) done(result);
      return;
    }

    // ── Exchange code for tokens ──────────────────────────────
    auto codeIt = cb.queryParams.find("code");
    if (codeIt == cb.queryParams.end() || codeIt->second.empty()) {
      result.success = false;
      result.error_message = "OAuth callback missing code parameter";
      if (done) done(result);
      return;
    }

    std::map<std::string, std::string> form;
    form["grant_type"]    = "authorization_code";
    form["code"]          = codeIt->second;
    form["redirect_uri"]  = redirectUri;
    form["client_id"]     = ::t3k::cloud::oauth::kClientId;
    form["code_verifier"] = verifier;
    const std::string body = ::t3k::net::urlEncodeQueryString(form);

    auto res = PostFormSync(::t3k::cloud::oauth::kTokenUrl, body);
    if (res.status_code < 200 || res.status_code >= 300) {
      result.success = false;
      result.error_message =
          "Token endpoint returned HTTP " +
          std::to_string(res.status_code) +
          (res.error_message.empty()
               ? std::string{}
               : (": " + res.error_message));
      if (done) done(result);
      return;
    }

    // ── Parse JSON response ───────────────────────────────────
    try {
      const std::string bodyStr(res.body.begin(), res.body.end());
      const json j = json::parse(bodyStr);
      if (j.contains("access_token")) {
        result.access_token = j.at("access_token").get<std::string>();
      }
      if (j.contains("refresh_token")) {
        result.refresh_token = j.at("refresh_token").get<std::string>();
      }
      if (j.contains("expires_in")) {
        result.expires_in = j.at("expires_in").get<int>();
      }
      if (result.access_token.empty()) {
        result.success = false;
        result.error_message =
            "Token endpoint response missing access_token";
      } else {
        result.success = true;
      }
    } catch (const std::exception& e) {
      result.success = false;
      result.error_message = std::string("Token response parse failed: ") +
                             e.what();
    }

    if (done) done(result);
  }).detach();
}

}  // namespace t3k::cloud
