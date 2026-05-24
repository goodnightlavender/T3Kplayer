# NOTICE — Third-Party Attributions

TONE3000 Player is a Windows VST3 fork of Steven Atkinson's Neural Amp Modeler Plugin. It inherits upstream's MIT licence and a fair amount of upstream code verbatim. This file lists everything we depend on, who wrote it, what licence it's distributed under, and what we use it for.

If you redistribute TONE3000 Player (binary or source), please preserve this file alongside the [LICENSE](./LICENSE).

---

## Upstream — Neural Amp Modeler

### Neural Amp Modeler Plugin

- **Project:** <https://github.com/sdatkinson/NeuralAmpModelerPlugin>
- **Licence:** MIT
- **Copyright:** © Steven Atkinson and contributors
- **Use:** This repository is a fork of the upstream plug-in. The plug-in scaffolding, audio I/O wiring, parameter system, sample-model handling, and the entire audio path are inherited verbatim. TONE3000 Player's contributions are confined to UI, library, cloud, networking, and sync modules.

### NeuralAmpModelerCore

- **Project:** <https://github.com/sdatkinson/NeuralAmpModelerCore>
- **Licence:** MIT
- **Copyright:** © Steven Atkinson and contributors
- **Use:** Vendored as a submodule. Provides the neural-amp-model inference engine — `.nam` file parsing, model construction, real-time audio processing. This is the heart of the audio path.

---

## Framework

### iPlug2

- **Project:** <https://github.com/iPlug2/iPlug2>
- **Licence:** WDL-OL (zlib-style) plus assorted permissive sub-licences for vendored dependencies (see `iPlug2/LICENSE.txt`)
- **Copyright:** © Oli Larkin and the iPlug2 contributors
- **Use:** The plug-in scaffolding, IGraphics drawing, parameter automation, audio I/O abstraction, and VST3 wrapper. Every `IControl` subclass in `ui/controls/` extends iPlug2's drawing primitives.

---

## Audio runtime dependencies

### Eigen

- **Project:** <https://eigen.tuxfamily.org/>
- **Licence:** MPL 2.0
- **Copyright:** © Eigen contributors
- **Use:** Inherited via NeuralAmpModelerCore — linear-algebra primitives underpinning NAM's inference math.

### dr_libs (dr_wav, dr_mp3, dr_flac)

- **Project:** <https://github.com/mackron/dr_libs>
- **Licence:** Public domain (Unlicense) / MIT-0 at your option
- **Copyright:** David Reid
- **Use:** Inherited via NeuralAmpModelerCore — single-header WAV / MP3 decoders used when loading impulse-response files.

---

## Plug-in vendored libraries

### nlohmann/json

- **Project:** <https://github.com/nlohmann/json>
- **Licence:** MIT
- **Copyright:** © Niels Lohmann and contributors
- **Use:** Vendored at `iPlug2/Dependencies/Extras/nlohmann/`. Drives sidecar-JSON read/write, OAuth token-response parsing, and the cross-device sync wire format.

### SQLite (amalgamation)

- **Project:** <https://sqlite.org/>
- **Licence:** Public domain
- **Copyright:** Public domain — see <https://sqlite.org/copyright.html>
- **Use:** Single-file `sqlite3.c` + `sqlite3.h` drop-in. Backs the local library database at `%LOCALAPPDATA%\TONE3000\library.db` — model metadata, tags, favourites, recents, display-name overrides, presets.

### NanoVG

- **Project:** <https://github.com/memononen/nanovg>
- **Licence:** zlib
- **Copyright:** © 2013 Mikko Mononen
- **Use:** Vendored inside iPlug2 (`iPlug2/Dependencies/IGraphics/NanoVG/`). Backs all 2D drawing in the redesigned UI — the slot tiles, SVG gear silhouettes, theme tokens, knobs, accordions, and rainbow scrubber are all rendered through NanoVG over OpenGL 2.

---

## Fonts

### Inter

- **Project:** <https://rsms.me/inter/>
- **Licence:** SIL Open Font Licence 1.1
- **Copyright:** © 2016 The Inter Project Authors, lead Rasmus Andersson
- **Use:** Body text throughout the UI — labels, captions, descriptions, tabular numeric read-outs on the knob row.

### Anton

- **Project:** <https://fonts.google.com/specimen/Anton>
- **Licence:** SIL Open Font Licence 1.1
- **Copyright:** © 2011 The Anton Project Authors, lead Vernon Adams
- **Use:** Display type — the TONE3000 wordmark, large section headings, and model titles in the info pane.

---

## Optional sync service (CloudFlare Worker)

### hono

- **Project:** <https://github.com/honojs/hono>
- **Licence:** MIT
- **Copyright:** © Yusuke Wada and contributors
- **Use:** HTTP routing for the optional `workers/library-sync/` CloudFlare Worker. Only required if a fork operator stands up cross-device sync; not bundled with the plug-in binary itself.

### CloudFlare Workers / D1

- **Project:** <https://developers.cloudflare.com/workers/> · <https://developers.cloudflare.com/d1/>
- **Licence:** Used as a hosted service (per CloudFlare's terms); SDK pieces (`@cloudflare/workers-types`, `wrangler`) are individually licensed (MIT / BSD — see their respective repositories).
- **Copyright:** © Cloudflare, Inc.
- **Use:** Hosting target for the optional library-sync Worker. Each fork operator deploys their own; there is no central instance.

---

## Service dependency (not bundled)

### TONE3000

- **Project:** <https://www.tone3000.com>
- **Licence:** TONE3000 is a third-party service; this plug-in is an unaffiliated client of its publicly documented OAuth 2.0 + REST API. TONE3000's name, logo, and catalogue content are property of their respective owners.
- **Use:** Source of the community NAM-profile catalogue, OAuth identity provider, and the brand whose visual language inspires the redesigned UI. TONE3000 Player has no business relationship with TONE3000; this is a third-party API client.

---

## License compatibility summary

All bundled and inherited components are licensed permissively (MIT, MPL 2.0, zlib, OFL 1.1, public domain). The aggregate is distributed under MIT per the upstream NAM plug-in's licence. MPL 2.0 (Eigen) is file-scoped and compatible with redistribution inside an MIT-licensed binary; no Eigen files have been modified.

<!-- TODO: if 0.1 ends up vendoring libcurl (per spec §6 dependency strategy), add it here — curl/libcurl, MIT/X-derived licence, © Daniel Stenberg et al. -->
<!-- TODO: if Catch2 ships with the source release (tests are excluded from binary), add it here — Catch2, BSL-1.0. -->
