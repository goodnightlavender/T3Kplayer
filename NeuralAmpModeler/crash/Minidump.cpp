// Minidump.cpp — Windows implementation of the Phase 9 crash filter.
//
// HARDENING NOTES
// ---------------
// The exception filter runs in a corrupted process state. The rules
// are strict:
//   * No heap allocation. The CRT heap may be poisoned by whatever
//     blew us up.
//   * No C++ exceptions. We're already unwinding from one.
//   * No locks. The crash thread may already hold them.
//   * Wide-string Win32 APIs only (CreateFileW, CreateDirectoryW) so
//     non-ASCII LOCALAPPDATA paths work.
//
// To honor "no allocation in the filter", we resolve the logs directory
// to a wide-string buffer ONCE at install time and stash it in a
// static `wchar_t[MAX_PATH]`. The filter itself only touches stack
// buffers, Win32 calls, and that pre-resolved path.

#if defined(_WIN32)

#include "Minidump.h"

// Win32 headers — keep windows.h before dbghelp.h.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <cstring>

// MiniDumpWriteDump lives in dbghelp.dll. Link it for any TU that
// pulls in this object.
#pragma comment(lib, "dbghelp.lib")

#include "../library/Paths.h"

namespace t3k::crash {

namespace {

// Resolved at install time, consulted from the SEH filter.
// MAX_PATH is fine because Paths::logsDir() is `%LOCALAPPDATA%\TONE3000\logs\`
// which fits comfortably; if the user has a pathological profile path
// that overflows we just bail on writing the dump (we keep the buffer
// empty and the filter short-circuits).
wchar_t g_logsDirW[MAX_PATH] = {0};

// Previous filter (returned by SetUnhandledExceptionFilter) — we chain
// to it after writing our dump so we cooperate with the host DAW's
// own handler if any.
LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;

// One-shot install guard. std::atomic<bool> rather than std::call_once
// because the latter can allocate on some implementations and we want
// zero allocation along the failure path.
std::atomic<bool> g_installed{false};

// Compose "<g_logsDirW>crash-YYYYMMDD-HHMMSS.dmp" into `out` (must be
// at least MAX_PATH wide). Returns the number of wchar_t written
// (excluding the null), or 0 on failure.
//
// Stack-only. No std::wstring, no swprintf with %ls of an external
// allocation — we write the digits by hand to keep this completely
// allocation-free.
size_t ComposeDumpPath(wchar_t* out, size_t cap, const SYSTEMTIME& st)
{
  if (!out || cap < 32) return 0;

  // Copy the prefix (g_logsDirW already ends in a backslash).
  const size_t prefixLen = wcsnlen(g_logsDirW, MAX_PATH);
  if (prefixLen == 0 || prefixLen + 32 >= cap) return 0;
  std::memcpy(out, g_logsDirW, prefixLen * sizeof(wchar_t));

  auto put2 = [](wchar_t* p, unsigned n) {
    p[0] = static_cast<wchar_t>(L'0' + (n / 10) % 10);
    p[1] = static_cast<wchar_t>(L'0' + n % 10);
  };
  auto put4 = [](wchar_t* p, unsigned n) {
    p[0] = static_cast<wchar_t>(L'0' + (n / 1000) % 10);
    p[1] = static_cast<wchar_t>(L'0' + (n / 100) % 10);
    p[2] = static_cast<wchar_t>(L'0' + (n / 10) % 10);
    p[3] = static_cast<wchar_t>(L'0' + n % 10);
  };

  // "crash-"
  wchar_t* p = out + prefixLen;
  static const wchar_t kPrefix[] = L"crash-";
  std::memcpy(p, kPrefix, (sizeof(kPrefix) - sizeof(wchar_t)));
  p += 6;

  put4(p, st.wYear);          p += 4;
  put2(p, st.wMonth);         p += 2;
  put2(p, st.wDay);           p += 2;
  *p++ = L'-';
  put2(p, st.wHour);          p += 2;
  put2(p, st.wMinute);        p += 2;
  put2(p, st.wSecond);        p += 2;

  static const wchar_t kExt[] = L".dmp";
  std::memcpy(p, kExt, sizeof(kExt));  // includes trailing null
  p += 4;

  return static_cast<size_t>(p - out);
}

// Resolve `Paths::logsDir()` (UTF-8) to a wide-string into g_logsDirW
// at install time. Best-effort; on failure we leave the buffer empty
// and the filter becomes a chain-only no-op.
void ResolveLogsDirW()
{
  const std::string utf8 = t3k::library::Paths::logsDir();
  if (utf8.empty()) return;

  const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                       static_cast<int>(utf8.size()),
                                       g_logsDirW, MAX_PATH - 1);
  if (wlen <= 0 || wlen >= MAX_PATH) {
    g_logsDirW[0] = L'\0';
    return;
  }
  g_logsDirW[wlen] = L'\0';

  // Mkdir the resolved path. CreateDirectoryW only creates the leaf;
  // we rely on ensureAppDataLayout() to create intermediates. If it
  // hasn't run yet this'll fail with ERROR_PATH_NOT_FOUND and the
  // CreateFileW in the filter will fail too — acceptable degradation.
  if (!CreateDirectoryW(g_logsDirW, nullptr)) {
    const DWORD err = GetLastError();
    if (err != ERROR_ALREADY_EXISTS) {
      // Leave the buffer set — directory may exist already (race) or
      // it doesn't, in which case writing a dump will fail cleanly.
    }
  }
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* eptrs)
{
  // If we never resolved a logs dir, skip straight to chaining.
  if (g_logsDirW[0] != L'\0' && eptrs != nullptr) {
    SYSTEMTIME st;
    GetSystemTime(&st);

    wchar_t path[MAX_PATH];
    path[0] = L'\0';
    const size_t len = ComposeDumpPath(path, MAX_PATH, st);
    if (len > 0) {
      HANDLE hFile = CreateFileW(path,
                                 GENERIC_WRITE,
                                 0,
                                 nullptr,
                                 CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL,
                                 nullptr);
      if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = eptrs;
        mei.ClientPointers    = FALSE;

        const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
            MiniDumpNormal | MiniDumpWithThreadInfo);

        MiniDumpWriteDump(GetCurrentProcess(),
                          GetCurrentProcessId(),
                          hFile,
                          type,
                          &mei,
                          nullptr,
                          nullptr);
        CloseHandle(hFile);
      }
    }
  }

  // Chain to the previously-installed filter so the host DAW's
  // handler (if any) can also do its work. If there was none, ask
  // Windows to terminate the process cleanly.
  if (g_prevFilter) {
    return g_prevFilter(eptrs);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

void installCrashHandler()
{
  bool expected = false;
  if (!g_installed.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel)) {
    return;  // already installed by an earlier instance
  }

  ResolveLogsDirW();
  g_prevFilter = SetUnhandledExceptionFilter(&CrashFilter);
}

}  // namespace t3k::crash

#else  // !_WIN32

namespace t3k::crash {
void installCrashHandler() {}
}  // namespace t3k::crash

#endif  // _WIN32
