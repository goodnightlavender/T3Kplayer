# AmpView — upstream parameter index capture

Captured 2026-05-22 from `NeuralAmpModeler/NeuralAmpModeler.h` lines 31-51
(`enum EParams`).

The 5 knobs ToneRoot wires (Input / Bass / Mid / Treble / Output) bind to
these symbolic indices from upstream NAM's parameter enum:

| Knob label | Upstream enum member | Numeric value |
|---|---|---|
| INPUT  | `kInputLevel`  | 0 |
| BASS   | `kToneBass`    | 2 |
| MID    | `kToneMid`     | 3 |
| TREBLE | `kToneTreble`  | 4 |
| OUTPUT | `kOutputLevel` | 5 |

(Index `1` is `kNoiseGateThreshold` — not exposed in the TONE3000 Player
header knob row. The noise-gate threshold control surfaces elsewhere in the
UI.)

## Source of truth

```cpp
// NeuralAmpModeler.h, lines 31-51
enum EParams
{
  // These need to be the first ones because I use their indices to place
  // their rects in the GUI.
  kInputLevel = 0,
  kNoiseGateThreshold,
  kToneBass,
  kToneMid,
  kToneTreble,
  kOutputLevel,
  // The rest is fine though.
  kNoiseGateActive,
  kEQActive,
  kIRToggle,
  // Input calibration
  kCalibrateInput,
  kInputCalibrationLevel,
  kOutputMode,
  kSlim,
  kNumParams
};
```

## How ToneRoot consumes this

When `ToneRoot.cpp` (Task 23) builds the persistent header knob row, it
constructs 5 `T3kKnob` controls and binds each to its upstream parameter via
the enum name (not the literal integer). Example sketch (to be written by the
ToneRoot dispatch):

```cpp
graphics.AttachControl(new T3kKnob(rInput,  kInputLevel,  "INPUT"));
graphics.AttachControl(new T3kKnob(rBass,   kToneBass,    "BASS"));
graphics.AttachControl(new T3kKnob(rMid,    kToneMid,     "MID"));
graphics.AttachControl(new T3kKnob(rTreble, kToneTreble,  "TREBLE"));
graphics.AttachControl(new T3kKnob(rOutput, kOutputLevel, "OUTPUT"));
```

Using the symbolic names (not raw integers) means this file stays in sync if
upstream NAM ever renumbers `EParams`.

## Why these indices matter

Binding T3kKnob to the same `EParams` values as stock NAM means:
- The DAW sees the same parameter automation IDs as before, so existing
  user automation lanes keep working.
- Preset save/load (which references parameter indices) round-trips
  identically.
- The DSP code in `NeuralAmpModeler.cpp` (`OnParamChange(int paramIdx)`)
  needs no changes for the new UI — the controls just push to the same
  parameter IDs.
