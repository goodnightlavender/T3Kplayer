// TokenStore.h — DPAPI-backed refresh-token persistence (Phase 5).
//
// Encrypts/decrypts the OAuth refresh token to/from
// `%LOCALAPPDATA%\TONE3000\tokens.dpapi`. Encryption uses Windows DPAPI
// (`CryptProtectData` / `CryptUnprotectData`) in user-scoped mode — no
// `CRYPTPROTECT_LOCAL_MACHINE` flag. A 16-byte fixed entropy salt
// (declared in TokenStore.cpp) ensures the ciphertext is bound to both
// the user account AND this application's salt; another DPAPI consumer
// running under the same Windows user can't decrypt our blob without
// also knowing the salt.
//
// Atomic-write strategy: encrypted data is written to
// `tokens.dpapi.tmp` then MoveFileExW'd with `MOVEFILE_REPLACE_EXISTING`
// onto `tokens.dpapi`. The store is single-writer in practice (one
// plug-in instance writes at a time during sign-in/sign-out), but the
// rename keeps us safe from torn writes on power loss.
//
// All three entry points are static — the store is stateless and
// re-entrant.
//
// Link: crypt32.lib.

#pragma once

#include <optional>
#include <string>

namespace t3k::cloud {

class TokenStore {
public:
  // Decrypts `tokens.dpapi` and returns the plaintext refresh token.
  // Returns std::nullopt if the file is missing OR if
  // CryptUnprotectData returned FALSE (corrupted blob, wrong user,
  // wrong salt). Never throws.
  static std::optional<std::string> loadRefreshToken();

  // Encrypts `token` via CryptProtectData (CRYPTPROTECT_UI_FORBIDDEN,
  // user-scoped) and atomic-writes to `tokens.dpapi`. Returns true on
  // success, false on any DPAPI / I/O failure.
  static bool saveRefreshToken(const std::string& token);

  // Deletes `tokens.dpapi` from disk. Idempotent — missing file is OK.
  static void clearRefreshToken();
};

}  // namespace t3k::cloud
