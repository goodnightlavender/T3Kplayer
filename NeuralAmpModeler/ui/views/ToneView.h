// ToneView — the Tone-tab body composite (v6 redesign, 2026-05-26).
//
// Two regions stacked top-to-bottom:
//   1. Signal-flow strip — 8 equal-width T3kModelTile tiles in three
//      flex-distributed groups (PEDALS 0..2 · AMP·CAB 3..4 · OUTBOARD
//      5..7) with a T3kGlobalKnob MASTER bookend on the right edge.
//   2. T3kFocusedSlot — focused-slot panel for the currently-selected
//      tile. Renders the model's image (portrait, cover-cropped, full
//      panel height), a title row with live T3kReadout, plus three
//      side-by-side columns: MODEL INFO (description + tags), SETTINGS
//      (2×3 knob grid bound to kToneBass/Mid/Treble/InputLevel/
//      OutputLevel/kDryWet), and METERS (vertical IN + OUT T3kVMeter
//      pair that self-update via OnMsgFromDelegate from the plugin's
//      input/output IPeakAvgSenders).
//
// Interactions:
//   - Click a tile        : focus + SetActiveSlot(dspSlot) — knobs
//                           re-bind to that slot via per-slot shadow.
//   - Double-click a tile : toggles bypass on both UI shadow and the
//                           live ExtraSlot::bypassed.
//   - Drag a tile         : reorders within the same category (pedals
//                           or outboard). Cross-category drops snap
//                           back. AMP / CAB are single-position.
//   - Click an empty tile : opens the Library tab (via onAddRequested).
//
// IsDirty() pulls per-frame ExtraSlot values into each Loaded tile so
// the strip's numerical readouts stay live; the meter levels are
// pushed via the senders' OnMsgFromDelegate path, not from IsDirty.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "IControl.h"
#include "IGraphics.h"

#include "../controls/T3kGearIcon.h"
#include "../controls/T3kModelTile.h"
#include "../controls/T3kGlobalKnob.h"
#include "../ModelInfoSnapshot.h"
#include "../../library/PresetState.h"

class NeuralAmpModeler;  // forward-declare upstream plug-in

namespace t3k::ui {

class T3kFocusedSlot;

class ToneView : public iplug::igraphics::IControl {
public:
  ToneView(const iplug::igraphics::IRECT& bounds, NeuralAmpModeler& plugin);

  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnResize() override;
  void OnAttached() override;
  // 2026-05-26 — every-frame pull of live ExtraSlot values into each Loaded
  // tile + meter-level placeholder into the focused panel. iPlug2 calls
  // IsDirty() every display refresh on every visible control; we hijack
  // the ToneView call to do the side-effect pull and forward to the base
  // implementation for the actual dirty flag.
  bool IsDirty() override;

  // iPlug2 attaches all controls flat — a hidden view does NOT propagate
  // to its children. Override Hide to manually propagate so tab-switches
  // don't leak the slot strip / info pane / knobs onto Library or Cloud.
  void Hide(bool hide) override;

  // Fired immediately after rebuildStrip() reattaches the strip tiles to
  // IGraphics. Lets the parent (ToneRoot) re-promote any control that
  // needs to stay above the strip in z-order (the preset overlay, in
  // particular — see ToneRoot::OnStripRebuilt). Optional.
  void setOnStripRebuilt(std::function<void()> cb) { mOnStripRebuilt = std::move(cb); }

  // Fires when the user clicks the "+" tile. ToneRoot wires this to
  // switchTab(Library) so the library tab acts as the model picker —
  // double-click / LOAD INTO CHAIN on a card brings the model back
  // here. Empty/null = no-op (the old hardcoded sample-pedal seed was
  // retired on 2026-05-25).
  void setOnAddSlotRequested(std::function<void()> cb) { mOnAddRequested = std::move(cb); }

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
      int        slotIndex;   // 0..kNumChainSlots-1 — chain UI index (visual slot)
      GearType   iconType;
      // 2026-05-26 — file kind, mirrors LibraryDb's `kind` column.
      // "nam" → routes through StageModelInSlot. "ir" → routes through
      // StageIRInSlot. Empty defaults to NAM for backwards-compat
      // with PresetState entries written before this field existed.
      std::string kind;
      std::string toneId;
      std::string modelId;
      std::string absPath;    // Absolute file path resolved from LibraryDb.uri,
                              // cached here so syncDspChain doesn't re-query.
      std::string imagePath;  // Local sibling image (t3k_image_path).
      std::string imageUrl;   // Remote image URL (t3k_image_url), resolved
                              // via cloud::ThumbnailCache when imagePath is empty.
      int        dspSlot = -1; // 0..kNumChainSlots-1 once staged, -1 otherwise.
      bool       bypassed = false;  // 2026-05-26 — UI shadow of ExtraSlot::bypassed
      double     dryWet   = 1.0;    // 2026-05-26 — UI shadow of ExtraSlot::dryWet
      ModelInfoSnapshot info;
    };
    std::vector<LoadedSlot> loaded;
    int selectedIndex = -1;   // slotIndex of the currently-selected tile
  };

  // ── Callbacks from child T3kModelTile tiles ───────────────────────
  void onSlotSelected(int slotIndex);
  void onSlotAdded(int slotIndex);
  // 2026-05-26 — double-click on a loaded tile flips both the UI shadow
  // (mChain.loaded[].bypassed) AND the live ExtraSlot.bypassed.
  void onSlotBypassToggle(int slotIndex);

  // Phase 10 — push the current chain into the DSP. Walks mChain.loaded
  // sorted by visual slotIndex, assigning DSP slots 0..kNumChainSlots-1.
  // Excess models (more than 5 loaded) are not staged into the audio
  // chain — they remain visible in the strip but are silent. Called
  // from loadModelIntoSlot and after a drag-reorder commit.
  void syncDspChain();

  // Drag-to-reorder: only pedal (slotIndex 0..2) and outboard (5..7)
  // tiles fire these. Within those categories the user can drag a tile
  // onto another same-category tile to swap positions. Drops outside
  // the category — or on the Amp/Cab single-position slots, or off the
  // strip entirely — are rejected and the tile snaps back.
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

  // Drag-bounds helper. After tile positions are computed in
  // rebuildStrip/layoutStripTiles, this iterates reorderable
  // categories (pedals, outboards) and pushes the (minOffset,
  // maxOffset) extent into each tile so its drag can't escape the
  // category's span on the strip.
  void updateDragBoundsForCategories();

  // ── Layout sub-rects ──────────────────────────────────────────────
  iplug::igraphics::IRECT mStripRect;
  iplug::igraphics::IRECT mFocusedRect;

  NeuralAmpModeler& mPlugin;

  ChainView mChain;

  // Children. The strip holds exactly kNumChainSlots tiles — pedal/amp/
  // cab/outboard positions show either a Loaded tile or an Empty
  // ("dashed +") tile. The focused-slot panel beneath owns the image,
  // title row, MODEL INFO / SETTINGS / METERS columns.
  std::vector<T3kModelTile*> mTiles;  // size == kNumChainSlots; null if not yet built
  T3kFocusedSlot*       mFocusedSlot = nullptr;
  T3kGlobalKnob*        mMasterKnob  = nullptr;  // 2026-05-26 — MASTER bookend on right edge of strip

  // Optional z-order-promotion notifier (see setOnStripRebuilt).
  std::function<void()> mOnStripRebuilt;
  // "+ tile" pressed — switches tabs to Library so the user can pick.
  std::function<void()> mOnAddRequested;
};

}  // namespace t3k::ui
