// Paths.cpp — Windows implementation of the AppData path resolver.
//
// Uses SHGetKnownFolderPath for LOCALAPPDATA + Documents (rather than
// the legacy %APPDATA% environment variable so Citrix / roaming-profile
// users land in the right place). All UTF-16 → UTF-8 conversion goes
// through WideCharToMultiByte with CP_UTF8.

#include "Paths.h"

#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <objbase.h>  // CoTaskMemFree

#include <filesystem>
#include <string>
#include <system_error>

namespace t3k::library {

namespace {

// Convert a wide string to UTF-8.
std::string Utf16To8(const wchar_t* w)
{
  if (!w) return {};
  const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (len <= 1) return {};
  std::string out(static_cast<size_t>(len - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
  return out;
}

// Resolve a known folder by REFKNOWNFOLDERID and append "\<sub>\" if
// the caller wants a child directory. Returns "" on failure.
std::string KnownFolder(REFKNOWNFOLDERID id, const wchar_t* subdir = nullptr)
{
  PWSTR p = nullptr;
  if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &p)) || !p) {
    if (p) CoTaskMemFree(p);
    return {};
  }
  std::wstring w(p);
  CoTaskMemFree(p);
  if (subdir) {
    w.push_back(L'\\');
    w.append(subdir);
  }
  w.push_back(L'\\');
  return Utf16To8(w.c_str());
}

}  // namespace

namespace Paths {

std::string appDataDir()
{
  return KnownFolder(FOLDERID_LocalAppData, L"TONE3000");
}

std::string libraryDbPath()
{
  const std::string base = appDataDir();
  if (base.empty()) return {};
  return base + "library.db";
}

std::string settingsPath()
{
  const std::string base = appDataDir();
  if (base.empty()) return {};
  return base + "settings.json";
}

std::string thumbnailCacheDir()
{
  const std::string base = appDataDir();
  if (base.empty()) return {};
  return base + "cache\\img\\";
}

std::string logsDir()
{
  const std::string base = appDataDir();
  if (base.empty()) return {};
  return base + "logs\\";
}

std::string suggestedToneRoot()
{
  return KnownFolder(FOLDERID_Documents, L"TONE3000");
}

void ensureAppDataLayout()
{
  // std::filesystem handles UTF-8 paths via the u8path overload and is
  // fine for "create-if-missing" semantics. Failures are silently
  // tolerated — first writer will see the error and we don't have a
  // logging surface this early in startup.
  namespace fs = std::filesystem;
  std::error_code ec;
  const std::string root = appDataDir();
  if (!root.empty()) {
    fs::create_directories(fs::u8path(root), ec);
    const std::string cache = thumbnailCacheDir();
    if (!cache.empty()) fs::create_directories(fs::u8path(cache), ec);
    const std::string logs = logsDir();
    if (!logs.empty())  fs::create_directories(fs::u8path(logs), ec);
  }
}

}  // namespace Paths

}  // namespace t3k::library
