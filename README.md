# T3K Player

T3K Player is a Windows VST3 fork of Steven Atkinson's Neural Amp Modeler
Plugin, focused on browsing, downloading, organizing, and playing TONE3000
models directly inside a DAW.

The fork keeps the NAM audio engine at its core and builds a purpose-made
player around it: a fixed tone chain, a local library, TONE3000 cloud browsing,
profile download management, presets, and optional cross-device sync.

## Features

- Windows VST3 plugin named `T3K Player`.
- Tone tab with pedal, amp head, cabinet, full rig, and outboard slots.
- Per-slot model settings, tone stack controls, dry/wet, bypass, delete, and
  drag ordering where the chain supports it.
- Library tab for downloaded TONE3000 content, local metadata edits, variants,
  and model loading.
- Cloud tab for signed-in TONE3000 catalog search, filtering, detail views, and
  queued downloads.
- Preset menu for saving and loading complete tone chains.
- Optional Cloudflare Worker plus D1 sync for downloaded-library metadata.
- Local reset flow that removes local settings, login state, caches, and models
  without deleting the remote D1 copy.

## Installation

Install the VST3 bundle to the standard system VST3 folder:

```powershell
C:\Program Files\Common Files\VST3\
```

Release builds are expected to provide both:

- `T3KPlayer.vst3`
- a Windows installer

After installing, rescan VST3 plugins in your DAW. T3K Player uses its own
plugin identifiers and resource path so it can be installed beside the main
Neural Amp Modeler plugin without sharing configuration.

## First Launch

New plugin instances open on the empty `Default` preset. This preset is always
kept empty and cannot be edited.

The first-run flow asks for a local TONE3000 library folder. Signing in is
optional, but enables the Cloud tab and restore/sync features.

Local state is stored under the T3K Player/TONE3000 application data area, not
inside the upstream NAM resource path.

## Building

The fork currently targets the VST3 build only.

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  '.\NeuralAmpModeler\NeuralAmpModeler.sln' `
  /p:Configuration=Release `
  /p:Platform=x64 `
  /t:NeuralAmpModeler-vst3 `
  /m /nologo /v:minimal
```

The Windows post-build step copies the built VST3 bundle to:

```powershell
C:\Program Files\Common Files\VST3\
```

## Optional Sync Worker

The sync worker lives in `workers/library-sync`. It uses Cloudflare Workers and
D1 to store signed-in user library metadata so another T3K Player install can
restore the user's downloaded-model list and metadata edits after login.

See `workers/library-sync/README.md` for deployment details.

## Relationship To NAM

T3K Player is a fork, not a replacement for Neural Amp Modeler. The upstream
project provides the model playback engine and core plugin foundation; this
fork adds a TONE3000-centered player workflow and UI.

Use upstream NAM when you want the canonical general-purpose NAM plugin. Use
T3K Player when you want the TONE3000 catalog, library, presets, and player
workflow inside the plugin.

## License

This project follows the upstream Neural Amp Modeler Plugin license. See
`LICENSE` and the third-party notices in the installer resources for details.

## Credits

- Steven Atkinson and the Neural Amp Modeler Plugin project.
- NeuralAmpModelerCore for NAM model playback.
- iPlug2 for the plugin framework.
- TONE3000 for the catalog and user account API consumed by this fork.
