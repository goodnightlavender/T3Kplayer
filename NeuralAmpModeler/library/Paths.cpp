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

#include <cstdio>
#include <filesystem>
#include <fstream>
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

std::string toneDir(const std::string& toneRoot, const std::string& toneId)
{
  namespace fs = std::filesystem;
  if (toneRoot.empty() || toneId.empty()) return {};
  fs::path p = fs::u8path(toneRoot);
  p /= fs::u8path(toneId);
  std::error_code ec;
  fs::create_directories(p, ec);
  // Always return a trailing-separator path so callers can string-
  // concat filenames directly. Mirrors the other Paths helpers.
  std::string out = pathToUtf8(p);
  if (!out.empty() && out.back() != '\\' && out.back() != '/') {
    out.push_back('\\');
  }
  return out;
}

bool atomicWriteFile(const std::string& path,
                     const unsigned char* data, std::size_t size)
{
  if (path.empty() || !data) return false;
  namespace fs = std::filesystem;

  const fs::path finalP   = fs::u8path(path);
  fs::path       partialP = finalP;
  partialP += ".partial";

  // Write bytes to .partial.
  {
    std::ofstream f(partialP, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    if (size > 0) {
      f.write(reinterpret_cast<const char*>(data),
              static_cast<std::streamsize>(size));
    }
    if (!f.good()) {
      f.close();
      std::error_code ec;
      fs::remove(partialP, ec);
      return false;
    }
  }

  // Rename onto final. ReplaceFileW + MoveFileExW would give a stronger
  // atomic guarantee on Windows, but fs::rename via std::filesystem is
  // adequate when both paths share a parent (same volume). A crash
  // between the write and the rename leaves a stranded .partial that
  // the next download / scan can clean up — the final file never sees
  // a half-written state.
  std::error_code ec;
  fs::rename(partialP, finalP, ec);
  if (ec) {
    // Cross-volume / locked-destination fallback.
    fs::copy_file(partialP, finalP,
                  fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    fs::remove(partialP, ec);  // best effort
  }
  return true;
}

bool deleteModelFiles(const std::string& namPath,
                      const std::string& sidecarPath,
                      const std::string& imagePath)
{
  namespace fs = std::filesystem;
  if (namPath.empty()) return false;
  std::error_code ec;

  const fs::path nam = fs::u8path(namPath);
  const fs::path parent = nam.parent_path();

  // Delete the nam file.
  fs::remove(nam, ec);  // best-effort; non-existent files are not errors

  // Delete the sidecar if a path was supplied.
  if (!sidecarPath.empty()) {
    fs::remove(fs::u8path(sidecarPath), ec);
  }

  // Image — only if it sits in the same per-tone directory as the
  // model. Refuse to delete anything that lives elsewhere; the user
  // may have provided their own gear photo from arbitrary disk
  // location.
  if (!imagePath.empty()) {
    const fs::path img = fs::u8path(imagePath);
    if (img.parent_path() == parent) {
      fs::remove(img, ec);
    }
  }

  // If the parent directory is now empty (we wiped the only entry),
  // prune it so the user's TONE3000 folder doesn't grow stale tone-id
  // subdirectories. Errors are ignored — if the directory still has
  // files (e.g. a manually-added README) `remove` returns false and
  // we move on.
  if (!parent.empty()) {
    fs::remove(parent, ec);
  }

  return true;
}

void revealInExplorer(const std::string& path)
{
  if (path.empty()) return;
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::exists(fs::u8path(path), ec)) return;

  // UTF-8 → UTF-16 for the ShellExecute argument.
  const int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(),
                                       static_cast<int>(path.size()),
                                       nullptr, 0);
  if (wlen <= 0) return;
  std::wstring wpath(static_cast<size_t>(wlen), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, path.c_str(),
                      static_cast<int>(path.size()),
                      wpath.data(), wlen);

  // `/select,"..."` opens Explorer with the target file selected.
  std::wstring params = L"/select,\"" + wpath + L"\"";
  ShellExecuteW(nullptr, L"open", L"explorer.exe",
                params.c_str(), nullptr, SW_SHOWNORMAL);
}

}  // namespace Paths

}  // namespace t3k::library
