// TokenStore.cpp — DPAPI implementation. See TokenStore.h.
//
// Encryption strategy:
//   - CryptProtectData with CRYPTPROTECT_UI_FORBIDDEN flag (suppresses
//     the legacy DPAPI consent dialog that almost no one wants to see).
//   - User-scoped (no CRYPTPROTECT_LOCAL_MACHINE flag) — the ciphertext
//     can only be decrypted by the SAME Windows user that wrote it.
//   - 16-byte fixed entropy salt (`kSalt` below) — another DPAPI
//     consumer running under the same Windows user can't decrypt the
//     blob without knowing the salt, even if they had read access to
//     the file.
//
// Atomic-write strategy:
//   - Write the ciphertext to `tokens.dpapi.tmp`.
//   - MoveFileExW(temp, final, MOVEFILE_REPLACE_EXISTING) — atomic on
//     NTFS, replaces the target if it already exists.
//
// Failure handling: every DPAPI / I/O call is checked; on any failure
// we wipe the temp file and return false (saveRefreshToken) or
// std::nullopt (loadRefreshToken). LocalFree the DATA_BLOB.pbData
// returned by both CryptProtectData and CryptUnprotectData.

#include "TokenStore.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "../library/Paths.h"

namespace t3k::cloud {

namespace {

// 16-byte entropy salt. Arbitrary fixed bytes — must NOT change between
// versions or saved tokens become undecryptable. (If we ever need to
// rotate, write a migration that decrypts with the old salt and
// re-encrypts with the new one before deleting the old file.)
static const BYTE kSalt[16] = {
    0xA7, 0x3D, 0x91, 0xE2, 0x4F, 0x18, 0xB0, 0x5C,
    0x6A, 0xDE, 0x29, 0x71, 0x85, 0xF4, 0x0B, 0xC6
};

// Resolve `%LOCALAPPDATA%\TONE3000\tokens.dpapi`. Returns empty string
// if appdata couldn't be located.
std::string TokensPath()
{
  const std::string root = ::t3k::library::Paths::appDataDir();
  if (root.empty()) return {};
  // Ensure the parent directory exists. ensureAppDataLayout is safe to
  // call repeatedly; first-touch lazily creates the directory tree.
  ::t3k::library::Paths::ensureAppDataLayout();
  return root + "tokens.dpapi";
}

// UTF-8 -> UTF-16 helper. Returns empty wstring on failure.
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

// Read a UTF-8 path's full byte contents. Returns empty vector on
// missing/failure; the caller checks fs::exists separately when it
// needs to distinguish "absent" from "I/O error".
std::vector<BYTE> ReadAllBytes(const std::string& path)
{
  namespace fs = std::filesystem;
  std::error_code ec;
  const auto p = fs::u8path(path);
  if (!fs::exists(p, ec) || ec) return {};

  const std::wstring wpath = Widen(path);
  if (wpath.empty()) return {};

  HANDLE h = ::CreateFileW(wpath.c_str(),
                           GENERIC_READ,
                           FILE_SHARE_READ,
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};

  LARGE_INTEGER sz = {};
  if (!::GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 ||
      sz.QuadPart > (1 << 20)) {  // hard cap 1 MiB; tokens are << 4 KiB
    ::CloseHandle(h);
    return {};
  }
  std::vector<BYTE> out(static_cast<size_t>(sz.QuadPart));
  DWORD got = 0;
  const BOOL ok = ::ReadFile(h, out.data(),
                             static_cast<DWORD>(out.size()), &got,
                             nullptr);
  ::CloseHandle(h);
  if (!ok || got != out.size()) return {};
  return out;
}

// Write `bytes` to `path` atomically: write to `path + ".tmp"` first,
// then MoveFileExW with MOVEFILE_REPLACE_EXISTING. Returns true on
// success.
bool WriteAllBytesAtomic(const std::string& path,
                         const BYTE* bytes, size_t len)
{
  const std::string tmp = path + ".tmp";
  const std::wstring tmpW = Widen(tmp);
  const std::wstring dstW = Widen(path);
  if (tmpW.empty() || dstW.empty()) return false;

  HANDLE h = ::CreateFileW(tmpW.c_str(),
                           GENERIC_WRITE,
                           0 /*no sharing during write*/,
                           nullptr,
                           CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;

  DWORD written = 0;
  const BOOL ok = ::WriteFile(h, bytes, static_cast<DWORD>(len),
                              &written, nullptr);
  ::CloseHandle(h);
  if (!ok || written != len) {
    ::DeleteFileW(tmpW.c_str());
    return false;
  }

  if (!::MoveFileExW(tmpW.c_str(), dstW.c_str(),
                     MOVEFILE_REPLACE_EXISTING)) {
    ::DeleteFileW(tmpW.c_str());
    return false;
  }
  return true;
}

}  // namespace

std::optional<std::string> TokenStore::loadRefreshToken()
{
  const std::string path = TokensPath();
  if (path.empty()) return std::nullopt;

  const auto cipher = ReadAllBytes(path);
  if (cipher.empty()) return std::nullopt;  // missing or unreadable

  DATA_BLOB inBlob;
  inBlob.pbData = const_cast<BYTE*>(cipher.data());
  inBlob.cbData = static_cast<DWORD>(cipher.size());

  DATA_BLOB salt;
  salt.pbData = const_cast<BYTE*>(kSalt);
  salt.cbData = sizeof(kSalt);

  DATA_BLOB outBlob = {};
  const BOOL ok = ::CryptUnprotectData(&inBlob,
                                       nullptr,           // description
                                       &salt,
                                       nullptr,           // reserved
                                       nullptr,           // prompt
                                       CRYPTPROTECT_UI_FORBIDDEN,
                                       &outBlob);
  if (!ok) return std::nullopt;

  std::string token(reinterpret_cast<const char*>(outBlob.pbData),
                    outBlob.cbData);
  ::LocalFree(outBlob.pbData);
  return token;
}

bool TokenStore::saveRefreshToken(const std::string& token)
{
  const std::string path = TokensPath();
  if (path.empty()) return false;

  DATA_BLOB inBlob;
  inBlob.pbData = reinterpret_cast<BYTE*>(
      const_cast<char*>(token.data()));
  inBlob.cbData = static_cast<DWORD>(token.size());

  DATA_BLOB salt;
  salt.pbData = const_cast<BYTE*>(kSalt);
  salt.cbData = sizeof(kSalt);

  DATA_BLOB outBlob = {};
  const BOOL ok = ::CryptProtectData(&inBlob,
                                     L"TONE3000 refresh token",
                                     &salt,
                                     nullptr,             // reserved
                                     nullptr,             // prompt
                                     CRYPTPROTECT_UI_FORBIDDEN,
                                     &outBlob);
  if (!ok) return false;

  const bool wrote = WriteAllBytesAtomic(path,
                                         outBlob.pbData,
                                         outBlob.cbData);
  ::LocalFree(outBlob.pbData);
  return wrote;
}

void TokenStore::clearRefreshToken()
{
  const std::string path = TokensPath();
  if (path.empty()) return;
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::remove(fs::u8path(path), ec);
  // ignore ec — idempotent
}

}  // namespace t3k::cloud
