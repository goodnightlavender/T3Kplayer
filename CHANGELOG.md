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

[Unreleased]: https://github.com/goodnightlavender/tone3000-player/compare/main...HEAD
