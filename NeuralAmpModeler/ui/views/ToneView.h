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
#include "../../library/PresetState.h"

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

  // iPlug2 attaches all controls flat — a hidden view does NOT propagate
  // to its children. Override Hide to manually propagate so tab-switches
  // don't leak the slot strip / info pane / knobs onto Library or Cloud.
  void Hide(bool hide) override;

  // Fired immediately after rebuildStrip() reattaches the strip tiles to
  // IGraphics. Lets the parent (ToneRoot) re-promote any control that
  // needs to stay above the strip in z-order (the preset overlay, in
  // particular — see ToneRoot::OnStripRebuilt). Optional.
  void setOnStripRebuilt(std::function<void()> cb) { mOnStripRebuilt = std::move(cb); }

  // ── Preset snapshot / apply (Phase 3) ───────────────────────────────
  // Snapshot the current chain + knob values into a PresetState ready
  // for PresetStore::saveCurrent.
  ::t3k::library::PresetState snapshotPresetState() const;

  // Apply a saved PresetState — resolves each slot's (tone_id, model_id)
  // through LibraryDb, rebuilds the chain, and writes the knob values
  // back to the upstream params via SendParameterValueFromUI.
  void applyPresetState(const ::t3k::library::PresetState& s);

  // ── Library-driven load ────────────────────────────────────────────
  // Insert / replace the model at `slotIndex` with the given TONE3000
  // ids. If slotIndex == -1, picks the next free pedal slot. Looked up
  // in LibraryDb; no-op for ids not found there.
  void loadModelIntoSlot(int slotIndex,
                         const std::string& toneId,
                         const std::string& modelId);

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

  // Drag-to-reorder: only pedal (slotIndex 0..4) and outboard (7..11)
  // tiles fire these. Within those categories the user can drag a tile
  // onto another same-category tile to swap positions. Drops outside
  // the category — or on the Amp/Cab single-position slots, or onto the
  // trailing "+" tile, or off the strip entirely — are rejected and the
  // tile snaps back.
  void onSlotDragMove(int slotIndex, float x, float y);
  void onSlotDragEnd (int slotIndex, float x, float y);

  // Rebuild the strip's child controls from mChain. Called on chain
  // changes (add/remove). NOT called from OnResize — see layoutStripTiles
  // for the in-place position update used during resize. Idempotent.
  void rebuildStrip();

  // Update positions on existing strip tiles in place — no detach/reattach.
  // Used by OnResize so the strip's z-order isn't disrupted by window
  // resizes (the earlier rebuildStrip-on-resize implementation pushed
  // tiles in front of later-attached controls like the preset overlay).
  void layoutStripTiles();

  // Common math used by rebuildStrip + layoutStripTiles: computes the
  // top-left coordinate where the centered strip should begin given the
  // current mChain and mStripRect.
  void computeStripLayout(float& outStartX, float& outTopY) const;

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

  // Optional z-order-promotion notifier (see setOnStripRebuilt).
  std::function<void()> mOnStripRebuilt;
};

}  // namespace t3k::ui
