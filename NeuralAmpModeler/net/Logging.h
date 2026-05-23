// Logging.h — tiny printf-style log macro for the net/ subsystem.
//
// Routes to Win32 OutputDebugStringA — visible in Visual Studio's
// Output pane and DebugView. Phase 9 swaps this header out for a real
// file-backed logger; everything that depends on it just keeps calling
// T3K_NET_LOG and stays oblivious.
//
// Levels are encoded as a bare string literal (`"info"`, `"warn"`,
// `"error"`, `"debug"`, `"trace"`) — the macro inlines it into the
// `[net][<level>] ` prefix. Cheap and obvious; no level filtering for
// 0.1 (DebugView's own filter does that job).
//
// Buffer is fixed 1024 chars on the stack — long log lines truncate
// silently. The net/ subsystem doesn't log payloads, just metadata
// (URLs, status codes, timings), so this is comfortable.

#pragma once

#include <cstdio>

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#define T3K_NET_LOG(level, fmt, ...)                                      \
  do {                                                                    \
    char _t3k_net_log_buf[1024];                                          \
    std::snprintf(_t3k_net_log_buf, sizeof(_t3k_net_log_buf),             \
                  "[net][" level "] " fmt "\n", ##__VA_ARGS__);           \
    ::OutputDebugStringA(_t3k_net_log_buf);                               \
  } while (0)
