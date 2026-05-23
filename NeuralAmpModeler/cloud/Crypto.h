// Crypto.h — BCrypt-backed SHA-256, base64url, and CSPRNG helpers for the
// Phase 5 OAuth 2.0 + PKCE flow.
//
// Everything here is thin Windows BCrypt wrapping. The SHA-256 algorithm
// provider handle is opened once at first use (Meyers static inside the
// anonymous namespace in Crypto.cpp) and reused for every hash.
//
// base64UrlEncode follows RFC 4648 §5 — the "url and filename safe"
// alphabet — and emits NO padding by default (PKCE prefers it that way,
// per RFC 7636 §4.2).
//
// makePkceVerifier produces 64 base64url characters from 48 random bytes
// (48 * 4/3 = 64), matching RFC 7636 §4.1's "43..128 chars from the
// unreserved set" guidance. makeStateNonce produces ~43 base64url chars
// from 32 random bytes — plenty of entropy for a single-use anti-CSRF
// nonce.
//
// Link: bcrypt.lib (added to every <Link> block in the vcxproj).
//
// Spec reference: docs/superpowers/specs/2026-05-21-tone3000-nam-fork-design.md
// §5 Phase 5, §9 Cloud client & OAuth.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace t3k::cloud {

// Fill a buffer with `n` random bytes from BCryptGenRandom with the
// system-preferred RNG (a CSPRNG). Returns an empty vector on failure.
std::vector<uint8_t> randomBytes(std::size_t n);

// RFC 4648 §5 base64url. `withPadding == false` (default) drops trailing
// '=' characters — what PKCE wants.
std::string base64UrlEncode(const uint8_t* data, std::size_t len,
                            bool withPadding = false);

// SHA-256 over `input`'s bytes. Returns the 32-byte raw digest. Returns
// an all-zero array on failure (caller can detect by checking
// `std::all_of(... 0 ...)` but in practice BCryptHashData on an open
// provider doesn't fail in this codepath).
std::array<uint8_t, 32> sha256(const std::string& input);

// PKCE verifier: 64 base64url chars sourced from 48 random bytes.
// RFC 7636 §4.1 — "43..128 chars from [A-Z][a-z][0-9]-._~". base64url
// stays inside that set (it adds "-" and "_" but not "." or "~", which
// is fine — the spec lets the verifier be a *subset* of the unreserved
// charset).
std::string makePkceVerifier();

// PKCE code_challenge for the S256 method: base64url(SHA256(verifier)).
std::string makePkceChallenge(const std::string& verifier);

// OAuth state nonce: 32 random bytes → ~43 base64url chars.
std::string makeStateNonce();

}  // namespace t3k::cloud
