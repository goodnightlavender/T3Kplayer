# Changelog

All notable changes to TONE3000 Player will be documented here. Format
loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added (Phase 1 — Fork preparation)

- Fork of upstream Neural Amp Modeler Plugin established under the
  TONE3000 Player name.
- Product strings (`PLUG_NAME`, `PLUG_MFR`, `BUNDLE_NAME`, `BUNDLE_MFR`,
  `BUNDLE_DOMAIN`) renamed in `NeuralAmpModeler/config.h`.
- Manufacturer set to Kai van Deursen.
- VST3 UID regenerated (`PLUG_UNIQUE_ID = 'T3kP'`, `PLUG_MFR_ID = 'T3k0'`)
  so the fork coexists with stock NAM in DAW scans.
- Windows `BINARY_NAME` set to `TONE3000Player` in
  `NeuralAmpModeler/config/NeuralAmpModeler-win.props` so the bundle
  filename on disk is `TONE3000Player.vst3`.
- `NOTICE` file with full upstream attribution.
- GitHub Actions workflow (`.github/workflows/build-windows.yml`)
  building VST3 on every push and pull request.
- Plugin version reset to `0.1.0` (`PLUG_VERSION_HEX = 0x00000100`).

### Changed

- `README.md` replaced with TONE3000 Player-specific content; upstream
  README preserved in git history.

### Added (Phase 3 — Local library subsystem)

- **SQLite-backed `library.db`** under `%LOCALAPPDATA%\TONE3000\`. WAL
  mode, foreign keys on. Schema lifted verbatim from spec §8.
- **First-run modal** picks a TONE3000 models folder (default
  `%USERPROFILE%\Documents\TONE3000\`). `settings.json` persists the
  choice.
- **`LibraryScanner`** walks the folder on a background thread;
  sidecar JSON (`.tone3000.json`) drives metadata. Manual "Rescan"
  button in the Library tab.
- **`LibraryView` rewritten** from placeholder to a real
  `T3kVScrollList` of model rows. Search-as-you-type filters by
  display name or creator.
- **Inline rename** via right-click → "Rename". Writes
  `display_name_override` to the local DB; `display_name` stays
  TONE3000-canonical. Renames persist across DAW restarts.
- **Click a library row** → loads the model into the next free
  pedal slot on the Tone tab.
- **Preset pill backed by `PresetStore`** — saved rigs round-trip
  through the `presets` SQLite table. `state_json` schema v2 captures
  the chain (no per-slot bypass/gain — Decision 44) + 5 tone-knob
  values.

### Notes

- Filesystem watcher (`ReadDirectoryChangesW`) is deferred — only
  manual Rescan ships in 0.1.
- Tags / favorites / recents tables are present in the schema but not
  surfaced in the UI yet.
- Image **downloading** lives in Phase 7. Phase 3 only renders images
  already on disk.

### Added (Phase 4 — Networking foundation)

- **`net::HttpClient`** — async GET/POST/PUT/DELETE over WinHTTP (Schannel
  TLS via the Windows stack). Auto-sized worker pool (2–4 threads from
  `hardware_concurrency`). Per-request cancellation tokens.
- **`net::RateLimiter`** — token bucket, 100 req/min default. Every
  request consults it before leaving the queue.
- **`net::ResponseCache`** — in-memory LRU (200 entries, 5 min TTL
  default; honors `Cache-Control: max-age`). On-disk single-file stub
  for thumbnails (`cache/img/`) — used only when callers opt in.
- **"Test net" button** in the Library tab — GET
  `https://www.gstatic.com/generate_204`, prints status + elapsed time.
  Phase-4-only diagnostic; will likely be removed before 0.1 ships.

### Notes

- Used WinHTTP instead of libcurl + Schannel (per spec §5 Phase 4).
  Reasoning: libcurl + static-link config is a significant Windows-build
  project; WinHTTP is built into the OS, supports TLS via Schannel
  natively, and the `HttpClient` interface stays libcurl-swap-friendly
  for future Mac/Linux ports.
- No real TONE3000 endpoints are hit this phase. Phase 5 wires the
  OAuth flow; Phase 6 consumes Cloud-tab endpoints.

[Unreleased]: https://github.com/goodnightlavender/tone3000-player/compare/main...HEAD
