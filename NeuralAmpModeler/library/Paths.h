// Paths.h — TONE3000 Player AppData layout resolver.
//
// All TONE3000-owned filesystem state lives under
// `%LOCALAPPDATA%\TONE3000\`. This header exposes the canonical paths so
// every consumer (LibraryDb, LibraryScanner, T3kFirstRunModal, Settings,
// future Phase 7 download cache) speaks the same dialect.
//
// All returned strings are UTF-8. On first call to ensureAppDataLayout()
// the appdata + cache + logs subdirectories are created if missing
// (`SHCreateDirectoryExW` semantics — already-exists is not an error).

#pragma once

#include <filesystem>
#include <string>

namespace t3k::library {

// Convert a std::filesystem::path to a UTF-8 std::string. Bridges a
// C++17→C++20 ABI change: C++20's `path::u8string()` returns
// `std::u8string` (a `basic_string<char8_t>`) which doesn't implicitly
// convert to `std::string`. This helper reinterprets the underlying
// bytes — safe because both share the same UTF-8 byte layout. Under
// C++17 the helper just forwards to the historical `path::u8string()`
// which already returns `std::string`.
inline std::string pathToUtf8(const std::filesystem::path& p)
{
#if defined(__cpp_lib_char8_t)
  const std::u8string u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
#else
  return p.u8string();
#endif
}

namespace Paths {

// `%LOCALAPPDATA%\TONE3000\` — appdata-root for the player. Trailing
// backslash included. Empty string on failure (no LOCALAPPDATA).
std::string appDataDir();

// `appDataDir() + "library.db"`.
std::string libraryDbPath();

// `appDataDir() + "settings.json"`.
std::string settingsPath();

// `appDataDir() + "cache\img\"`. Trailing backslash included.
std::string thumbnailCacheDir();

// `appDataDir() + "logs\"`. Trailing backslash included.
std::string logsDir();

// `%USERPROFILE%\Documents\TONE3000\`. Trailing backslash included.
// This is the default offered by T3kFirstRunModal; the user can pick
// any other directory.
std::string suggestedToneRoot();

// Creates the appdata + cache + logs subdirectories if they don't yet
// exist. Idempotent. Safe to call from any thread.
void ensureAppDataLayout();

}  // namespace Paths

}  // namespace t3k::library
