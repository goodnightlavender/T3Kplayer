// Crypto.cpp — BCrypt implementation. See Crypto.h.
//
// SHA-256 provider strategy: BCryptOpenAlgorithmProvider with
// BCRYPT_SHA256_ALGORITHM is opened lazily at first use and cached in a
// Meyers static inside the anonymous namespace. Closing the provider is
// intentionally skipped — the process exits before it would matter, and
// `BCryptCloseAlgorithmProvider` during DllMain (which can happen on
// VST3 plugin unload in some hosts) has been known to deadlock.
//
// BCryptGenRandom with BCRYPT_USE_SYSTEM_PREFERRED_RNG doesn't require
// an algorithm handle — Windows resolves the RNG provider internally.
//
// base64url uses the unpadded "URL-safe" alphabet from RFC 4648 §5
// (replaces '+'/'/' with '-'/'_'). PKCE explicitly recommends no
// padding (RFC 7636 §4.2).

#include "Crypto.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#include <cstring>

namespace t3k::cloud {

namespace {

// Cached SHA-256 provider handle, opened on first call to ShaProvider().
// Returns nullptr only if BCryptOpenAlgorithmProvider fails — which on a
// healthy Win10/11 install never happens. Callers should null-check the
// returned handle defensively anyway.
BCRYPT_ALG_HANDLE ShaProvider()
{
  static BCRYPT_ALG_HANDLE h = []() -> BCRYPT_ALG_HANDLE {
    BCRYPT_ALG_HANDLE provider = nullptr;
    NTSTATUS s = ::BCryptOpenAlgorithmProvider(
        &provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (s != 0) return nullptr;
    return provider;
  }();
  return h;
}

// RFC 4648 §5 — URL-safe alphabet (no padding by default).
constexpr const char* kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

}  // namespace

std::vector<uint8_t> randomBytes(std::size_t n)
{
  if (n == 0) return {};
  std::vector<uint8_t> out(n);
  NTSTATUS s = ::BCryptGenRandom(
      nullptr,
      out.data(),
      static_cast<ULONG>(n),
      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  if (s != 0) return {};
  return out;
}

std::string base64UrlEncode(const uint8_t* data, std::size_t len,
                            bool withPadding)
{
  if (!data || len == 0) return {};

  std::string out;
  // Output size: ceil(len / 3) * 4 — with padding; minus 1..2 if dropped.
  out.reserve(((len + 2) / 3) * 4);

  std::size_t i = 0;
  while (i + 3 <= len) {
    const uint32_t triple =
        (static_cast<uint32_t>(data[i])     << 16) |
        (static_cast<uint32_t>(data[i + 1]) << 8)  |
        (static_cast<uint32_t>(data[i + 2]));
    out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
    out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
    out.push_back(kAlphabet[(triple >>  6) & 0x3F]);
    out.push_back(kAlphabet[triple & 0x3F]);
    i += 3;
  }

  // Tail: 1 or 2 leftover bytes.
  const std::size_t rem = len - i;
  if (rem == 1) {
    const uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
    out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
    out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
    if (withPadding) { out.push_back('='); out.push_back('='); }
  } else if (rem == 2) {
    const uint32_t triple =
        (static_cast<uint32_t>(data[i])     << 16) |
        (static_cast<uint32_t>(data[i + 1]) << 8);
    out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
    out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
    out.push_back(kAlphabet[(triple >>  6) & 0x3F]);
    if (withPadding) out.push_back('=');
  }

  return out;
}

std::array<uint8_t, 32> sha256(const std::string& input)
{
  std::array<uint8_t, 32> digest{};

  BCRYPT_ALG_HANDLE alg = ShaProvider();
  if (!alg) return digest;

  BCRYPT_HASH_HANDLE hash = nullptr;
  NTSTATUS s = ::BCryptCreateHash(alg, &hash, nullptr, 0,
                                  nullptr, 0, 0);
  if (s != 0 || !hash) return digest;

  if (!input.empty()) {
    s = ::BCryptHashData(
        hash,
        // BCryptHashData wants PUCHAR (writable) but doesn't mutate.
        const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(input.data())),
        static_cast<ULONG>(input.size()),
        0);
    if (s != 0) {
      ::BCryptDestroyHash(hash);
      return digest;
    }
  }

  s = ::BCryptFinishHash(hash, digest.data(),
                         static_cast<ULONG>(digest.size()), 0);
  ::BCryptDestroyHash(hash);
  if (s != 0) {
    // Zero the buffer so the caller sees a clean failure signal.
    std::memset(digest.data(), 0, digest.size());
  }
  return digest;
}

std::string makePkceVerifier()
{
  // 48 random bytes → 64 base64url chars (48 * 4/3 = 64). Inside the
  // PKCE-allowed character set [A-Za-z0-9-_~.] (we use a subset that
  // skips '.' and '~' — fine per RFC 7636 §4.1 which permits any
  // subset of the unreserved chars).
  const auto bytes = randomBytes(48);
  if (bytes.empty()) return {};
  return base64UrlEncode(bytes.data(), bytes.size(), /*withPadding*/ false);
}

std::string makePkceChallenge(const std::string& verifier)
{
  if (verifier.empty()) return {};
  const auto digest = sha256(verifier);
  return base64UrlEncode(digest.data(), digest.size(),
                         /*withPadding*/ false);
}

std::string makeStateNonce()
{
  // 32 random bytes → 43 base64url chars (no padding).
  const auto bytes = randomBytes(32);
  if (bytes.empty()) return {};
  return base64UrlEncode(bytes.data(), bytes.size(), /*withPadding*/ false);
}

}  // namespace t3k::cloud
