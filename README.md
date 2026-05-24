# TONE3000 Player

> A modern, TONE3000-integrated Windows VST3 fork of Steven Atkinson's [Neural Amp Modeler Plugin](https://github.com/sdatkinson/NeuralAmpModelerPlugin) — browse, download, and play community NAM profiles and impulse responses without leaving your DAW.

![](docs/screenshots/cloud-tab.png)

**Version:** 0.1.0 · **Platform:** Windows (VST3) · **License:** MIT

---

## What this is

TONE3000 Player keeps Steven Atkinson's NAM audio engine and tonestack DSP intact and wraps them in a redesigned, tabbed UI that talks directly to [tone3000.com](https://www.tone3000.com)'s public OAuth + REST API. Sign in once, browse the community catalogue from inside your DAW, download a tone, and play through it — all in one window.

- **Tone** tab — signal chain (pedals · amp · cab · outboard, hard-capped at 5/1/1/5) plus the five tone knobs (input, bass, mid, treble, output).
- **Library** tab — search, rename, and load tones you've already downloaded.
- **Cloud** tab — search, filter, and one-click-download from tone3000.com.
- **Account** — OAuth 2.0 + PKCE sign-in through your system browser. Refresh tokens encrypted with Windows DPAPI under your user profile.

All file and network I/O runs off the audio thread.

---

## Install

1. Grab `TONE3000Player.vst3` (a directory-shaped VST3 bundle) from the [Releases](../../releases) page.
2. Copy the bundle into your system VST3 folder. From an **elevated PowerShell** (Run as administrator):

   ```powershell
   Copy-Item -Recurse -Force `
     ".\TONE3000Player.vst3" `
     "C:\Program Files\Common Files\VST3\"
   ```

   Verify it landed:

   ```powershell
   Get-ChildItem "C:\Program Files\Common Files\VST3" -Directory `
     | Where-Object Name -like "TONE3000*"
   ```

3. Launch your DAW. If it doesn't pick up the new plug-in automatically, trigger a VST3 rescan in your DAW's plug-in preferences.

The plug-in writes user state to `%LOCALAPPDATA%\TONE3000\` (`library.db`, `cache\img\`, `logs\`, and the DPAPI-encrypted `tokens.dpapi`). Nothing escapes your machine unless you sign in to TONE3000 or opt in to optional cross-device sync.

---

## First-run setup

The first time you instantiate TONE3000 Player on a fresh install, a small first-run modal asks you to:

1. **Pick a library folder** — where downloaded tones and their sidecar JSON files live. Default suggestion: `%USERPROFILE%\Documents\TONE3000\`. You can change this later in Settings.
2. **Sign in (optional but recommended)** — click the avatar pill in the top-right of the header. Your system browser opens, you sign in on tone3000.com, and the browser hands the token back through a one-shot loopback listener on `127.0.0.1:53000` (falls back to `53001`–`53009` if the port is busy).

Signed-in state survives DAW restarts. Sign out from the avatar dropdown when you want it gone — that wipes the local token file.

You don't have to sign in to use the plug-in: pre-downloaded tones still load and play offline. Signing in is what lights up the Cloud tab and (if you've stood one up) cross-device sync.

---

## Use

### Tone tab

The Tone tab is the everyday view. The signal-chain strip across the top holds your loaded gear left-to-right; the info pane below shows the selected tile's metadata; the five tone knobs sit beneath that.

- **Add a model** — click the `+` tile on the strip. The model picker opens with everything in your library; flip to "Cloud" inside the picker to pull something new.
- **Inspect a model** — click any loaded tile. Its image, creator, tags and description load into the info pane.
- **Remove a model** — hover the tile, click the `×` that appears in its top-right corner. One undo step lands on the global undo stack (`↶` in the header).
- **Knobs** — input, bass, mid, treble, output, with tabular numeric read-outs (`0.0 dB`, `+2.0`, etc.). DAW automation works as usual.

### Library tab

Everything you've downloaded, searchable. You can rename a row inline — the new name is a display-name override stored locally; the canonical TONE3000 sidecar JSON is left alone. Renames sync across machines if you've set up the optional Worker (see below).

### Cloud tab

Live browsing of tone3000.com — search-as-you-type with a 250ms debounce, accordion filters, infinite-scroll pagination. Click **Download** on a card and the download enters the queue (shown as a pill near the avatar). When it finishes, the file lands in your library folder with sidecar JSON + the model's primary image, and the Library tab refreshes.

---

## Cross-device sync (optional)

If you use the plug-in on more than one machine, you can stand up your own CloudFlare Worker + D1 database so your library state (downloaded tones, favourites, recents, rename overrides, slot assignments) follows you across installs. On a fresh machine, signing in offers to re-download everything you previously had.

This is **entirely optional**. The plug-in works exactly the same without it. There is **no central sync service** — each operator (you, or a fork maintainer running things for a small community) deploys their own Worker. CloudFlare's free tier is dramatically over-provisioned for personal use.

Full deployment runbook: [`workers/library-sync/README.md`](./workers/library-sync/README.md).

---

## Build from source

Verified Windows runbook (toolchain, dependencies, build steps, install, and DAW verification) lives at [`docs/setup/BUILD-WIN.md`](../docs/setup/BUILD-WIN.md). If you only want a working binary, grab one from Releases; the source build is for contributors and re-forkers.

---

## Troubleshoot

| Symptom | What's happening | Fix |
|---|---|---|
| Plug-in doesn't appear in the DAW after install | The DAW hasn't rescanned its VST3 folders | In your DAW's plug-in preferences, ensure "Use VST3 plug-in system folders" is enabled and click Rescan. In Ableton Live: Options → Preferences → Plug-Ins → Rescan Plug-Ins. |
| Sign-in browser tab opens but never returns | Firewall or AV is blocking the loopback HTTP listener on `127.0.0.1:53000`–`53009` | Allow inbound on that port range for the plug-in host process. Some corporate firewalls block all loopback traffic; on those, sign-in won't work and you'll need to use the plug-in offline. |
| Download fails or stalls | TONE3000 rejected the request, the model URL expired, or disk is full | Check the Cloud tab's status banner — it surfaces the specific failure reason. Pre-signed URLs are refetched immediately before each download, so retrying usually clears expiry errors. |
| Plug-in loads but produces silence | No model is loaded into the amp slot | Open the Tone tab, click `+`, pick a model from Library or Cloud. |
| Cloud tab is empty | You're not signed in | Click the "Sign in" pill in the header. |

If something else is broken, please open an issue with your DAW name + version, Windows build, and (if relevant) the contents of `%LOCALAPPDATA%\TONE3000\logs\`.

---

## What's NOT in 0.1

Lifted verbatim from the design spec (§3 Non-goals):

- Mac/Linux builds, AU, AAX, CLAP formats (Windows VST3 first; clean cross-platform code).
- In-plugin upload / capture creation / model training.
- Writing favorites or ratings back to the TONE3000 server.
- Purely local file management. The library tab manages only files downloaded through the plugin from TONE3000.
- Visual regression tests.
- Telemetry, crash reporting service, in-plugin auto-update.
- Code signing of the installer (deferred to 0.2).
- Localization (strings infrastructure is plumbed; only English ships).

---

## License & credits

This project is released under the **MIT License**, inherited from upstream NAM. See [`LICENSE`](./LICENSE) for the full text and [`NOTICE.md`](./NOTICE.md) for full per-dependency attribution.

Special thanks:

- **[Steven Atkinson](https://github.com/sdatkinson)** — author of [Neural Amp Modeler Plugin](https://github.com/sdatkinson/NeuralAmpModelerPlugin) and [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore). The audio engine in TONE3000 Player is his work, used verbatim.
- **[iPlug2](https://github.com/iPlug2/iPlug2)** — the plug-in framework everything is built on.
- **[tone3000.com](https://www.tone3000.com)** — the community catalogue and OAuth + REST API this plug-in consumes. TONE3000 is operated independently of this project; this is a third-party client of their public API, not an officially endorsed product.

---

## Contributing

Bug reports and small fixes welcome via the [issue templates](.github/ISSUE_TEMPLATE) and pull requests against `main`. For anything larger than a one-file change, please open an issue first so we can talk it through.

See [`CONTRIBUTING.md`](./CONTRIBUTING.md) for branch conventions, the commit-message format, and the code-review checklist.

<!-- TODO: confirm CONTRIBUTING.md ships with 0.1 — currently referenced but may not be authored yet. -->
<!-- TODO: confirm issue templates exist under .github/ISSUE_TEMPLATE/ before release. -->
<!-- TODO: replace docs/screenshots/cloud-tab.png placeholder with a real screenshot before tagging v0.1.0. -->
