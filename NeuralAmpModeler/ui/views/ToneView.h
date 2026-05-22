// ToneView — the Tone-tab body composite (Phase 2b).
//
// Replaces the Phase 2 AmpView + IRView duo. Owns three regions stacked
// top-to-bottom:
//   1. Slot strip — a horizontal row of T3kSlot tiles (one per loaded
//      slot in ChainView) plus a single trailing Add tile.
//   2. T3kModelInfoPane — inspector for whichever slot is selected.
//   3. Knob row — the 5 persistent tone knobs (Input / Bass / Mid /
//      Treble / Output) bound to the upstream EParams indices. Moved
//      out of ToneRoot per Decision 35.
//
// Phase-2b limitations:
//   - mChain is seeded from seedDemoSnapshot() — no real signal-chain
//     reads yet. Phase 3 wires audio::SignalChain.
//   - The Add tile appends a hard-coded sample model rather than opening
//     a real picker overlay.
//   - The Remove callback drops the entry locally without any undo
//     persistence yet.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "IControl.h"
#include "IGraphics.h"

#include "../controls/T3kGearIcon.h"
#include "T3kModelInfoPane.h"

class NeuralAmpModeler;  // forward-declare upstream plug-in

namespace t3k::ui {

class T3kSlot;
class T3kKnob;

class ToneView : public iplug::igraphics::IControl {
public:
  ToneView(const iplug::igraphics::IRECT& bounds, NeuralAmpModeler& plugin);

  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnResize() override;
  void OnAttached() override;

private:
  // ── Chain snapshot ────────────────────────────────────────────────
  struct ChainView {
    struct LoadedSlot {
      int        slotIndex;   // 0..11 — chain DSP index
      GearType   iconType;
      std::string toneId;
      std::string modelId;
      ModelInfoSnapshot info;
    };
    std::vector<LoadedSlot> loaded;
    int selectedIndex = -1;   // slotIndex of the currently-selected tile
  };

  // ── Callbacks from child T3kSlot tiles ────────────────────────────
  void onSlotSelected(int slotIndex);
  void onSlotRemoved(int slotIndex);
  void onSlotAdded();

  // Rebuild the strip's child controls from mChain. Called on resize and
  // whenever mChain changes (add/remove). Idempotent.
  void rebuildStrip();

  // Drop all existing strip child controls (detached + deleted via
  // IGraphics) before a rebuild.
  void clearStripChildren();

  // Seed mChain with the v6 mockup's demo chain.
  void seedDemoSnapshot();

  // ── Layout sub-rects ──────────────────────────────────────────────
  iplug::igraphics::IRECT mStripRect;
  iplug::igraphics::IRECT mInfoRect;
  iplug::igraphics::IRECT mKnobRect;

  NeuralAmpModeler& mPlugin;

  ChainView mChain;

  // Children. The slot strip tiles + Add tile are recreated on each
  // chain change; the info pane and knobs live for the lifetime of
  // ToneView.
  std::vector<T3kSlot*> mSlots;     // loaded slot tiles, sized to mChain.loaded
  T3kSlot*              mAddTile = nullptr;
  T3kModelInfoPane*     mInfoPane = nullptr;
  T3kKnob*              mKnobIn = nullptr;
  T3kKnob*              mKnobBass = nullptr;
  T3kKnob*              mKnobMid = nullptr;
  T3kKnob*              mKnobTreble = nullptr;
  T3kKnob*              mKnobOut = nullptr;
};

}  // namespace t3k::ui
