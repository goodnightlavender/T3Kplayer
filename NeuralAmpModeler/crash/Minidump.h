// Minidump.h — Phase 9 crash-minidump filter for TONE3000 Player.
//
// Installs a process-wide SetUnhandledExceptionFilter that writes a
// timestamped .dmp into `%LOCALAPPDATA%\TONE3000\logs\` when the VST3
// hits an unhandled SEH exception. The filter chains to any
// previously-installed filter, so it cooperates politely with the host
// DAW's own crash handler if one is present.
//
// Windows-only at 0.1 (TONE3000 Player only builds for Win32 right now).
// All symbols are guarded so the header can be included unconditionally
// from cross-platform call sites — on non-Win32 the function is a no-op.

#pragma once

namespace t3k::crash {

// Idempotent. Safe to call multiple times — second and subsequent
// calls are no-ops (guarded by a std::atomic<bool>). On non-Windows
// platforms this is a no-op stub.
//
// Call once during plug-in construction, after Paths::ensureAppDataLayout()
// has had a chance to run (so the logs/ dir exists). The handler is
// process-wide, so subsequent NeuralAmpModeler instances re-installing
// the filter is harmless.
void installCrashHandler();

}  // namespace t3k::crash
