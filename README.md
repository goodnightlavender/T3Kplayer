# T3K Player

T3K Player is a fork of Steven Atkinson's Neural Amp Modeler, focused on
browsing,  downloading, organizing, and playing TONE3000 models directly
inside your DAW.

The fork keeps the NAM audio engine at its core and builds a purpose-made
player around it: a fixed tone chain, a local library, TONE3000 cloud browsing,
presets, and cross-device sync.

## Features

- Tone tab with 8 model slots, each with their own settings.
- Library tab for downloaded TONE3000 content.
- Cloud tab for signed-in TONE3000 catalog search and downloads.
- Preset menu for saving and loading complete tone chains.
- Library and metadata account sync.

## Installation

Install the VST3 bundle to the standard system VST3 folder:

```powershell
C:\Program Files\Common Files\VST3\
```

## First Launch

New plugin instances open on the empty `Default` preset.

The first-run flow asks for a local TONE3000 library folder. Signing in with your
TONE3000 account is required, and enables the Cloud tab's functionality.

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

## Relationship To NAM

T3K Player is a fork, not a replacement for Neural Amp Modeler. The upstream
project provides the model playback engine and core plugin foundation; this
fork adds a polished workflow and UI with cloud functionality.

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
- TONE3000 for the catalog and user account API used in this fork.
