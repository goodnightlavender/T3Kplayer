# TONE3000 Player

> A modern, TONE3000-integrated fork of [Neural Amp Modeler Plugin](https://github.com/sdatkinson/NeuralAmpModelerPlugin) with in-plug-in browsing, downloading, and library management of TONE3000 community NAM profiles and impulse responses.

**Status:** Pre-release (0.1.0 in development). Windows VST3 only at this stage.

## What this is

TONE3000 Player is a fork of Steve Atkinson's excellent open-source [Neural Amp Modeler plugin](https://github.com/sdatkinson/NeuralAmpModelerPlugin). The audio engine is unchanged — same NAM inference, same tonestack, same DSP quality. What's new:

- A redesigned UI mirroring [tone3000.com](https://www.tone3000.com)'s visual language.
- An in-plug-in **Cloud** tab: search, filter, preview, and one-click-download NAM profiles and IRs from TONE3000 without leaving your DAW.
- A **Library** tab: tag, favorite, and browse your downloaded TONE3000 content with metadata intact.
- OAuth 2.0 + PKCE sign-in via your system browser; refresh tokens stored securely (Windows DPAPI).
- All network and disk I/O strictly off the audio thread.

## Installation

See [`docs/setup/BUILD-WIN.md`](./docs/setup/BUILD-WIN.md) for the verified build runbook. Pre-built installers will land on the [Releases](../../releases) page when 0.1.0 ships.

## Project layout

- `NeuralAmpModeler/` — plugin source (upstream's tree, with our additions under `ui/`, `library/`, `cloud/`, `net/`, `settings/`, `tests/` planned across Phases 2–4)
- `iPlug2/`, `NeuralAmpModelerCore/`, `AudioDSPTools/`, `eigen/` — upstream submodules
- `docs/setup/` — build runbook and discovered-layout doc
- `docs/superpowers/specs/` — design specifications (private dev notes)
- `docs/superpowers/plans/` — implementation plans

## Building from source

Short version (Windows):

1. Install Visual Studio 2022 or 2026 Community with the C++ workload + Windows 11 SDK.
2. Clone with `--recurse-submodules`.
3. From Git Bash, run `bash iPlug2/Dependencies/IPlug/download-vst3-sdk.sh` once.
4. Open `NeuralAmpModeler/NeuralAmpModeler.sln` in Visual Studio.
5. Set Configuration = Release, Platform = x64.
6. Right-click `NeuralAmpModeler-vst3` → Build.
7. Copy the resulting `.vst3` bundle to `C:\Program Files\Common Files\VST3\` (admin required).

Full instructions: [`docs/setup/BUILD-WIN.md`](./docs/setup/BUILD-WIN.md).

## Credits

This project would not exist without [Steve Atkinson](https://github.com/sdatkinson)'s [Neural Amp Modeler Plugin](https://github.com/sdatkinson/NeuralAmpModelerPlugin), which provides the entire audio engine. Steve's work is released under the MIT License and remains the upstream of this fork. See [`NOTICE`](./NOTICE) for the full list of upstream dependencies and their licenses.

TONE3000 ([tone3000.com](https://www.tone3000.com)) hosts the community library this plug-in browses. TONE3000 is operated independently of this project — this is a third-party client of their public API, not an officially endorsed product.

## License

MIT — see [`LICENSE`](./LICENSE). Inherited from upstream NAM.

## Contributing

Pre-release stage; contribution guidelines will solidify after 0.1.0 ships. For now: file an issue before working on anything substantive.
