// PresetState.h — POD describing a single saved Tone-tab preset.
//
// Mirrors the spec §8 "presets.state_json schema" (v2). Empty tone_id
// means the slot is empty in the saved chain.
//
// Round-trips through PresetStore via nlohmann/json. Schema-1 state
// blobs are migrated forward by silently dropping the dropped fields
// (PresetStore.cpp).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace t3k::library {

struct PresetState {
  struct SlotEntry {
    int         slotIndex = 0;    // 0..11 — chain DSP index
    std::string toneId;           // empty => slot is unloaded
    std::string modelId;
  };
  std::vector<SlotEntry> slots;

  // 2026-05-26 — global master output gain in dB. Default 0 (unity).
  double master_output_db = 0.0;

  struct Knobs {
    float input_db  = 0.f;
    float bass      = 0.f;
    float mid       = 0.f;
    float treble    = 0.f;
    float output_db = 0.f;
  };
  Knobs knobs;

  // state_json schema version (== 2 since Decision 44).
  static constexpr int kSchemaVersion = 2;
};

}  // namespace t3k::library
