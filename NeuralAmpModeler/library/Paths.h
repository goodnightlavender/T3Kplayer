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

// Returns `<toneRoot>/<toneId>/` and creates the directory if needed.
// Used by cloud::Downloader to land model files + sidecars in a
// per-tone subfolder. `toneId` is the canonical TONE3000 tone id as
// a string (digits at this revision); we don't sanitize because the
// upstream is server-issued.
std::string toneDir(const std::string& toneRoot, const std::string& toneId);

// Crash-safe write: serialize `data`/`size` bytes to `<path>.partial`,
// then rename onto `path` (replacing any existing file). Returns
// false on any I/O failure. The partial sentinel survives a crash
// so the next download / library scan can clean it up. Used by
// cloud::Downloader for the model bodies + cloud::ThumbnailCache
// for the image cache.
bool atomicWriteFile(const std::string& path,
                     const unsigned char* data, std::size_t size);

// Best-effort filesystem cleanup for a library entry. Deletes:
//   - `namPath` itself
//   - `sidecarPath` (the .tone3000.json next to it) when non-empty
//   - `imagePath` when non-empty AND lives in the same parent directory
//     as `namPath` (we never delete user-managed images that live
//     elsewhere).
// Empty / nonexistent paths are silently ignored. Returns true if the
// nam file itself was removed (or was already absent).
bool deleteModelFiles(const std::string& namPath,
                      const std::string& sidecarPath = {},
                      const std::string& imagePath   = {});

// Opens File Explorer focused on `path`'s parent directory with `path`
// pre-selected (Win32 `explorer.exe /select,...`). No-op when `path` is
// empty or doesn't exist on disk.
void revealInExplorer(const std::string& path);

}  // namespace Paths

}  // namespace t3k::library
