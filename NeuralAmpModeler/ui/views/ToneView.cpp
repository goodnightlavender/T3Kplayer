// ToneView.cpp — see ToneView.h.

#include "ToneView.h"

#include <algorithm>
#include <cmath>

#include "../theme.h"
#include "../layout.h"
#include "../controls/T3kModelTile.h"
#include "T3kFocusedSlot.h"

// Plug-in header — gives us the EParams enum (kInputLevel, kToneBass, ...).
#include "../../NeuralAmpModeler.h"

#include "../../library/LibraryDb.h"
#include "../../library/PresetState.h"

namespace t3k::ui {

using namespace iplug::igraphics;

namespace {

// 2026-05-26 polish-pass — strip + tile sizing scaled ~1.5× from the
// 720 px v6 mockup so the chain strip reads correctly at the plug-in's
// ~1024 px design canvas. Old (96 / 64 / 8 / 16) values produced
// disproportionately small tiles in the actual rendered plug-in.
//
//   strip height: 96 → 148 (reserves a top 18 px band for category
//                            labels PEDALS / AMP / CABINET / OUTBOARD)
//   master knob : 64 → 88
//
// kStripPadH adds inner horizontal padding so the leftmost tile no
// longer flushes against the window edge (fixes the "first tile clipped"
// report) — mirrors the `.t3k-flow{padding:8px 6px}` chrome in the
// mockup, scaled up.
constexpr float kStripH        = 148.f;
constexpr float kStripGap      = 8.f;
constexpr float kStripPadH     = 10.f;   // inner left/right padding inside the strip
constexpr float kStripLabelBandH = 18.f;  // top band reserved for group labels
constexpr float kMasterW       = 88.f;
constexpr float kMasterGap     = 12.f;   // space between rack and master knob

// Inter-tile gap inside a single category group (pedals / amp+cab / outboard).
constexpr float kTileGapWithinGroup = 8.f;
// Gap BETWEEN the three category groups (visual separator between pedals,
// amp+cab, and outboard).
constexpr float kGroupGap = 16.f;

// 2026-05-26 — new 8-slot chain schema (was 12-slot):
//   indices 0..2 → pedals  (category 0, reorderable)
//   index 3     → amp     (category 1, single-position)
//   index 4     → cab     (category 3, single-position)
//   indices 5..7 → outboard (category 2, reorderable)
constexpr int kPedalSlotMin    = 0;
constexpr int kPedalSlotMax    = 2;
constexpr int kAmpSlot         = 3;
constexpr int kCabSlot         = 4;
constexpr int kOutboardSlotMin = 5;
constexpr int kOutboardSlotMax = 7;

int slotCategory(int slotIndex)
{
  if (slotIndex >= kPedalSlotMin    && slotIndex <= kPedalSlotMax)    return 0;
  if (slotIndex == kAmpSlot)                                          return 1;
  if (slotIndex == kCabSlot)                                          return 3;
  if (slotIndex >= kOutboardSlotMin && slotIndex <= kOutboardSlotMax) return 2;
  return -1;
}

// Only categories with more than one possible position support reorder.
bool isReorderableCategory(int slotIndex)
{
  const int c = slotCategory(slotIndex);
  return c == 0 || c == 2;
}

// Map a slot index to the gear icon shown when the tile is Empty.
GearType emptyGearFor(int slotIndex)
{
  switch (slotCategory(slotIndex)) {
    case 0:  return GearType::Pedal;
    case 1:  return GearType::Amp;
    case 3:  return GearType::Cab;
    case 2:  return GearType::Outboard;
    default: return GearType::Pedal;
  }
}

}  // namespace

ToneView::ToneView(const IRECT& bounds, NeuralAmpModeler& plugin)
: IControl(bounds), mPlugin(plugin)
{
  // Phase 2b smoke-test seed retired on 2026-05-25 — the chain stays
  // empty until the user loads something from the Library tab.
  OnResize();
}

void ToneView::OnResize()
{
  // Top strip (kStripH px) sits above the focused-slot panel (the rest).
  mStripRect   = IRECT(mRECT.L, mRECT.T,            mRECT.R, mRECT.T + kStripH);
  mFocusedRect = IRECT(mRECT.L, mStripRect.B + kStripGap, mRECT.R, mRECT.B);

  if (mFocusedSlot) mFocusedSlot->SetTargetAndDrawRECTs(mFocusedRect);
  // Position the strip's tiles in place (no z-order disruption).
  layoutStripTiles();
}

void ToneView::OnAttached()
{
  IGraphics* g = GetUI();
  if (!g) return;

  // Focused-slot panel — created once, lives for ToneView's lifetime.
  mFocusedSlot = new T3kFocusedSlot(mFocusedRect, mPlugin);
  g->AttachControl(mFocusedSlot);

  // MASTER knob — bookends the right edge of the strip. Bound directly to
  // ::kMasterOutput so drag/wheel/host-automation all converge through
  // iPlug2's standard IKnobControlBase path. Positioned by layoutStripTiles.
  mMasterKnob = new T3kGlobalKnob(IRECT(0.f, 0.f, 1.f, 1.f),
                                  ::kMasterOutput, "MASTER");
  g->AttachControl(mMasterKnob);

  // Strip tiles — built AFTER the focused slot so their click events
  // (which trigger setSnapshot on mFocusedSlot) always have a live panel
  // to push into.
  rebuildStrip();

  // Reflect initial selection in the focused panel.
  if (mChain.selectedIndex >= 0) {
    onSlotSelected(mChain.selectedIndex);
  }
}

void ToneView::Draw(IGraphics& /*g*/)
{
  // The strip + focused-slot panel paint themselves; ToneView is purely
  // a layout shell with no chrome of its own.
}

bool ToneView::IsDirty()
{
  // When the Tone tab isn't visible, skip the per-frame ExtraSlot pull
  // entirely. Saves N tile-setter calls and the meter-level fan-out on
  // every display refresh while the user is on the Library or Cloud tab.
  if (IsHidden()) return IControl::IsDirty();

  // Push the live ExtraSlot values for each loaded entry into its tile so
  // the strip's per-slot numeric readouts stay in sync with knob changes
  // (which may come from the focused panel, the host, or a preset apply).
  // iPlug2's IsDirty is called every display refresh — side-effecting here
  // gives us per-frame updates while still forwarding to the base
  // dirty-flag mechanism so ToneView itself only repaints when needed.
  for (auto& ls : mChain.loaded)
  {
    T3kModelTile::Values v;
    if (const auto* es = mPlugin.GetExtraSlot(ls.dspSlot))
    {
      v.bass   = static_cast<int>(std::round(es->bass));
      v.mid    = static_cast<int>(std::round(es->mid));
      v.treble = static_cast<int>(std::round(es->treble));
      v.inDb   = static_cast<int>(std::round(es->inGainDb));
      v.outDb  = static_cast<int>(std::round(es->outGainDb));
      v.dryWet = static_cast<int>(std::round(es->dryWet * 100.0));
    }
    if (ls.slotIndex >= 0 && ls.slotIndex < static_cast<int>(mTiles.size())
        && mTiles[ls.slotIndex])
    {
      mTiles[ls.slotIndex]->setValues(v);
    }
  }

  // 2026-05-26 (Phase G2) — meters self-update via iPlug2's tagged routing
  // from NeuralAmpModeler's mInputSender/mOutputSender to each T3kVMeter's
  // OnMsgFromDelegate. No fan-out from ToneView needed; placeholder zero-
  // push removed.

  return IControl::IsDirty();
}

void ToneView::Hide(bool hide)
{
  IControl::Hide(hide);
  // iPlug2 attaches all controls flat — hiding this view does NOT
  // auto-propagate. Cascade to every child so the strip / focused panel
  // don't leak onto Library or Cloud tabs. T3kFocusedSlot does its own
  // child-cascade for the knobs / meters it owns.
  for (T3kModelTile* t : mTiles) if (t) t->Hide(hide);
  if (mFocusedSlot) mFocusedSlot->Hide(hide);
  if (mMasterKnob)  mMasterKnob ->Hide(hide);
}

void ToneView::clearStripChildren()
{
  IGraphics* g = GetUI();
  if (!g) return;
  for (T3kModelTile* t : mTiles) {
    if (t) g->RemoveControl(t);
  }
  mTiles.clear();
}

void ToneView::computeStripLayout(float& outStartX, float& outTopY) const
{
  // Layout split: rack (8 equal-width tiles in 3 groups) on the left,
  // MASTER knob (kMasterW) on the right with kMasterGap between them.
  // Vertical: the top kStripLabelBandH band is reserved for category
  // labels; tiles live in the remaining height.
  const float tileBandT = mStripRect.T + kStripLabelBandH;
  const float tileBandH = mStripRect.B - tileBandT;
  const float tileH  = std::min(tileBandH - 4.f, 122.f);
  outStartX = mStripRect.L + kStripPadH;
  outTopY   = tileBandT + (tileBandH - tileH) * 0.5f;
}

void ToneView::layoutStripTiles()
{
  if (mTiles.empty()) return;
  if (static_cast<int>(mTiles.size()) != ::kNumChainSlots) return;

  // MASTER knob owns the right edge (vertically centered in the full strip).
  const IRECT masterR(mStripRect.R - kStripPadH - kMasterW, mStripRect.T,
                      mStripRect.R - kStripPadH,            mStripRect.B);
  if (mMasterKnob) mMasterKnob->SetTargetAndDrawRECTs(masterR);

  // 2026-05-26 polish-pass — reserve the top kStripLabelBandH band for the
  // PEDALS / AMP / CABINET / OUTBOARD group labels (drawn in Draw()). Tiles
  // live in the remainder, vertically centered.
  const float tileBandT = mStripRect.T + kStripLabelBandH;
  const float tileBandB = mStripRect.B;
  const float tileBandH = tileBandB - tileBandT;
  const float tileH = std::min(tileBandH - 4.f, 122.f);
  const float topY  = tileBandT + (tileBandH - tileH) * 0.5f;

  // The rack hosts the 8 equal-width tiles between kStripPadH inset and
  // the MASTER knob bookend. The earlier implementation flushed the first
  // tile to mStripRect.L which sat the leftmost icon against the window
  // edge — now we pad inward by kStripPadH on both sides.
  const float rackL = mStripRect.L + kStripPadH;
  const float rackR = masterR.L - kMasterGap;
  const float rackW = rackR - rackL;
  const float tileW =
      (rackW - 2.f * kGroupGap - 5.f * kTileGapWithinGroup) / 8.f;

  // Walk the 8 slots in chain order, inserting a kGroupGap when we cross
  // pedals→amp/cab (idx 2→3) and amp/cab→outboard (idx 4→5).
  float x = rackL;
  for (int i = 0; i < ::kNumChainSlots; ++i) {
    if (i == kAmpSlot)         x += kGroupGap - kTileGapWithinGroup;
    if (i == kOutboardSlotMin) x += kGroupGap - kTileGapWithinGroup;
    if (mTiles[i]) {
      mTiles[i]->SetTargetAndDrawRECTs(IRECT(x, topY, x + tileW, topY + tileH));
    }
    x += tileW + kTileGapWithinGroup;
  }

  updateDragBoundsForCategories();
}

void ToneView::rebuildStrip()
{
  IGraphics* g = GetUI();
  if (!g) return;

  // Always exactly kNumChainSlots tiles — Loaded if a chain entry occupies
  // that slot index, Empty otherwise. The Empty variant draws a dashed +
  // and routes click → onSlotAdded(slotIndex).
  clearStripChildren();
  mTiles.assign(::kNumChainSlots, nullptr);

  const IRECT ph(0.f, 0.f, 1.f, 1.f);

  for (int i = 0; i < ::kNumChainSlots; ++i) {
    // Find the loaded entry at this slot index (if any).
    auto it = std::find_if(
        mChain.loaded.begin(), mChain.loaded.end(),
        [i](const ChainView::LoadedSlot& s) { return s.slotIndex == i; });

    const bool      isLoaded  = (it != mChain.loaded.end());
    const GearType  iconType  = isLoaded ? it->iconType : emptyGearFor(i);
    const auto      variant   = isLoaded
        ? T3kModelTile::Variant::Loaded
        : T3kModelTile::Variant::Empty;

    auto* tile = new T3kModelTile(
        ph, /*slotIndex*/ i, variant, iconType,
        /*onSelect*/        [this](int slot) { onSlotSelected(slot); },
        /*onAdd*/           [this](int slot) { onSlotAdded(slot); },
        /*onBypassToggle*/  [this](int slot) { onSlotBypassToggle(slot); });

    if (isLoaded) {
      tile->setName(it->info.displayName);
      tile->setSelected(i == mChain.selectedIndex);
      tile->setBypassed(it->bypassed);
    }

    // Only pedal / outboard tiles support drag-to-reorder. Amp / Cab live
    // at fixed positions; their drag callbacks stay null (T3kModelTile's
    // OnMouseDrag still fires for Loaded tiles, but with no callbacks it
    // just visually offsets and snaps back on mouse-up).
    if (isLoaded && isReorderableCategory(i)) {
      tile->setOnDragMove([this](int slot, float mx, float my) {
        onSlotDragMove(slot, mx, my);
      });
      tile->setOnDragEnd([this](int slot, float mx, float my) {
        onSlotDragEnd(slot, mx, my);
      });
    }

    g->AttachControl(tile);
    mTiles[i] = tile;
  }

  // Position the freshly-attached tiles and seed their drag bounds.
  layoutStripTiles();

  // The rebuild appended new tiles to the end of the IGraphics control
  // list. If anything was attached later (e.g. the preset overlay), that
  // control would now sit BELOW the strip in z-order. Notify any listener
  // (ToneRoot, currently) so it can re-promote overlays that should stay
  // on top.
  if (mOnStripRebuilt) mOnStripRebuilt();
}

void ToneView::updateDragBoundsForCategories()
{
  // For each reorderable tile, find the leftmost-L and rightmost-R of
  // any same-category tile in the current layout, then express the
  // dragged tile's allowable offset range relative to its own mRECT.L.
  //
  // mTiles is now indexed BY slot (always kNumChainSlots entries) — not
  // by chain position — so the loop is over slot indices directly.
  if (static_cast<int>(mTiles.size()) != ::kNumChainSlots) return;
  for (int i = 0; i < ::kNumChainSlots; ++i) {
    if (!mTiles[i]) continue;
    if (!isReorderableCategory(i)) continue;
    const int cat = slotCategory(i);

    float catLeft  = mTiles[i]->GetRECT().L;
    float catRight = mTiles[i]->GetRECT().R;
    for (int j = 0; j < ::kNumChainSlots; ++j) {
      if (!mTiles[j]) continue;
      if (slotCategory(j) != cat) continue;
      catLeft  = std::min(catLeft,  mTiles[j]->GetRECT().L);
      catRight = std::max(catRight, mTiles[j]->GetRECT().R);
    }

    const float baseL = mTiles[i]->GetRECT().L;
    const float tileW = mTiles[i]->GetRECT().W();
    mTiles[i]->setDragBoundsX(catLeft - baseL,
                              catRight - tileW - baseL);
  }
}

void ToneView::onSlotSelected(int slotIndex)
{
  mChain.selectedIndex = slotIndex;
  for (int i = 0; i < static_cast<int>(mTiles.size()); ++i) {
    if (mTiles[i]) mTiles[i]->setSelected(i == slotIndex);
  }
  // Find the loaded entry corresponding to this slot and push its snapshot
  // into the focused-slot panel. Empty selection clears the panel.
  auto it = std::find_if(
      mChain.loaded.begin(), mChain.loaded.end(),
      [slotIndex](const ChainView::LoadedSlot& s) {
        return s.slotIndex == slotIndex;
      });
  if (it != mChain.loaded.end()) {
    if (mFocusedSlot) mFocusedSlot->setSnapshot(it->info);
    if (it->dspSlot >= 0) mPlugin.SetActiveSlot(it->dspSlot);
  } else if (mFocusedSlot) {
    mFocusedSlot->clear();
  }
}

void ToneView::onSlotBypassToggle(int slotIndex)
{
  auto it = std::find_if(
      mChain.loaded.begin(), mChain.loaded.end(),
      [slotIndex](const ChainView::LoadedSlot& s) {
        return s.slotIndex == slotIndex;
      });
  if (it == mChain.loaded.end()) return;
  it->bypassed = !it->bypassed;
  if (auto* es = mPlugin.GetExtraSlot(it->dspSlot)) {
    es->bypassed = it->bypassed;
  }
  if (slotIndex >= 0 && slotIndex < static_cast<int>(mTiles.size())
      && mTiles[slotIndex]) {
    mTiles[slotIndex]->setBypassed(it->bypassed);
  }
  SetDirty(false);
}

void ToneView::onSlotDragMove(int /*slotIndex*/, float /*x*/, float /*y*/)
{
  // No-op for Phase 2b. The dragged tile already paints itself shifted
  // (see T3kSlot::Draw), which is enough visual feedback for the smoke
  // test. Phase 3 can layer in a drop-target indicator here (e.g. paint a
  // 2px kAccent strip on the leading edge of whichever same-category
  // tile is currently under the cursor).
}

void ToneView::onSlotDragEnd(int slotIndex, float x, float y)
{
  // Source = the loaded entry whose slot index is slotIndex.
  auto srcIt = std::find_if(mChain.loaded.begin(), mChain.loaded.end(),
                            [slotIndex](const ChainView::LoadedSlot& s) {
                              return s.slotIndex == slotIndex;
                            });
  if (srcIt == mChain.loaded.end()) {
    rebuildStrip();  // safety: re-snap the tile back to its original rect
    return;
  }
  const int srcCat = slotCategory(slotIndex);

  // Destination = the slot whose tile contains the drop point.
  int dstSlot = -1;
  for (int i = 0; i < static_cast<int>(mTiles.size()); ++i) {
    if (mTiles[i] && mTiles[i]->GetRECT().Contains(x, y)) {
      dstSlot = i;
      break;
    }
  }
  if (dstSlot < 0 || dstSlot == slotIndex) {
    rebuildStrip();
    return;
  }
  if (slotCategory(dstSlot) != srcCat) {
    rebuildStrip();
    return;
  }

  // Swap-or-shift within the category. If the destination is occupied,
  // swap slot indices; otherwise just move the dragged entry there.
  auto dstIt = std::find_if(mChain.loaded.begin(), mChain.loaded.end(),
                            [dstSlot](const ChainView::LoadedSlot& s) {
                              return s.slotIndex == dstSlot;
                            });
  srcIt->slotIndex = dstSlot;
  if (dstIt != mChain.loaded.end() && dstIt != srcIt) {
    dstIt->slotIndex = slotIndex;
  }
  mChain.selectedIndex = dstSlot;

  rebuildStrip();
  syncDspChain();
  if (mChain.selectedIndex >= 0) onSlotSelected(mChain.selectedIndex);
}

void ToneView::onSlotAdded(int /*slotIndex*/)
{
  // The Empty tile click now opens the Library tab — the user picks a
  // model there and clicks LOAD INTO CHAIN to land it back in this strip.
  // We currently ignore the slot index; loadModelIntoSlot picks the first
  // free pedal slot. Wiring the click to pre-target a specific slot is
  // a Phase G/X follow-up.
  if (mOnAddRequested) mOnAddRequested();
}

::t3k::library::PresetState ToneView::snapshotPresetState() const
{
  ::t3k::library::PresetState s;
  s.slots.reserve(mChain.loaded.size());
  for (const auto& ls : mChain.loaded) {
    ::t3k::library::PresetState::SlotEntry e;
    e.slotIndex = ls.slotIndex;
    e.toneId    = ls.toneId;
    e.modelId   = ls.modelId;
    s.slots.push_back(std::move(e));
  }

  // Knob values pulled straight from the upstream params (native units).
  auto readParam = [this](int idx) -> float {
    if (auto* p = mPlugin.GetParam(idx)) {
      return static_cast<float>(p->Value());
    }
    return 0.f;
  };
  s.knobs.input_db  = readParam(::kInputLevel);
  s.knobs.bass      = readParam(::kToneBass);
  s.knobs.mid       = readParam(::kToneMid);
  s.knobs.treble    = readParam(::kToneTreble);
  s.knobs.output_db = readParam(::kOutputLevel);

  // 2026-05-26 — new global + per-slot fields.
  if (auto* p = mPlugin.GetParam(::kMasterOutput))
    s.master_output_db = p->Value();
  // s.slots was built 1:1 from mChain.loaded above — the second bound is dead.
  for (size_t i = 0; i < mChain.loaded.size(); ++i)
  {
    s.slots[i].bypassed = mChain.loaded[i].bypassed;
    if (const auto* es = mPlugin.GetExtraSlot(mChain.loaded[i].dspSlot))
      s.slots[i].dryWet = es->dryWet;
    else
      s.slots[i].dryWet = 1.0;
  }
  return s;
}

void ToneView::applyPresetState(const ::t3k::library::PresetState& s)
{
  // ── Knobs: write through SendParameterValueFromUI so the audio
  // thread + host (DAW automation lane) both see the change. ─────────
  auto writeParam = [this](int idx, float v) {
    if (auto* p = mPlugin.GetParam(idx)) {
      p->Set(v);
      mPlugin.SendParameterValueFromUI(idx, p->ToNormalized(v));
    }
  };
  writeParam(::kInputLevel,  s.knobs.input_db);
  writeParam(::kToneBass,    s.knobs.bass);
  writeParam(::kToneMid,     s.knobs.mid);
  writeParam(::kToneTreble,  s.knobs.treble);
  writeParam(::kOutputLevel, s.knobs.output_db);

  // ── Chain: rebuild mChain from the preset's slot list. Empty slot
  // entries (toneId == "") are dropped; resolvable ids become real
  // LoadedSlot rows hydrated from LibraryDb. Phase 3 doesn't yet
  // wire the model into the audio chain — this only mirrors the v6
  // UI snapshot. (Audio wiring lands with Phase 5's SignalChain.) ────
  mChain.loaded.clear();
  for (const auto& e : s.slots) {
    if (e.toneId.empty() && e.modelId.empty()) continue;
    auto row = ::t3k::library::LibraryDb::instance()
                 .findByToneAndModelId(e.toneId, e.modelId);
    if (!row.has_value()) continue;

    ChainView::LoadedSlot ls;
    ls.slotIndex            = e.slotIndex;
    ls.toneId               = e.toneId;
    ls.modelId              = e.modelId;
    // Derive icon type from gear_type — fall back to Pedal.
    if (row->gear_type == "amp")        ls.iconType = GearType::Amp;
    else if (row->gear_type == "cab")   ls.iconType = GearType::Cab;
    else if (row->gear_type == "outboard") ls.iconType = GearType::Outboard;
    else if (row->gear_type == "full-rig") ls.iconType = GearType::FullRig;
    else                                   ls.iconType = GearType::Pedal;
    ls.kind                = row->kind;  // 2026-05-26 — see loadModelIntoSlot.
    ls.bypassed            = e.bypassed;
    ls.dryWet              = e.dryWet;
    // 2026-05-26 — also restore absPath + image fields so the audio
    // chain gets re-staged on the syncDspChain() call below. Without
    // this, applyPresetState was a UI-only snapshot — restoring a
    // preset showed the slot strip correctly but produced silence.
    ls.absPath             = row->uri;
    if (row->t3k_image_path.has_value()) ls.imagePath = *row->t3k_image_path;
    ls.imageUrl            = row->t3k_image_url;
    ls.info.imagePath      = ls.imagePath;
    ls.info.imageUrl       = ls.imageUrl;
    ls.info.displayName    = row->effectiveDisplayName();
    ls.info.creator        = row->t3k_creator;
    ls.info.format         = (row->kind == "ir") ? "IR" : "NAM";
    ls.info.sizeBytes      = row->size_bytes;
    ls.info.downloadedAtMs = row->added_at;
    ls.info.description    = row->t3k_description;
    mChain.loaded.push_back(std::move(ls));
  }
  // The Phase-2b fallback that seeded a demo chain when the preset
  // came back empty was retired on 2026-05-25 — it dropped sample
  // pedals into the strip on every "Default Setting" recall. The
  // chain now stays empty until the user explicitly loads from the
  // Library tab.

  rebuildStrip();
  syncDspChain();

  // 2026-05-26 — restore per-slot dry/wet + bypass to the DSP after staging.
  // Both reads use mChain.loaded[i].* (the aligned UI shadow set during the
  // construction loop above) — NOT s.slots[i].* which would be misaligned
  // because the construction loop skips empty/unresolvable preset entries.
  for (size_t i = 0; i < mChain.loaded.size(); ++i)
  {
    if (auto* es = mPlugin.GetExtraSlot(mChain.loaded[i].dspSlot))
    {
      es->dryWet   = mChain.loaded[i].dryWet;
      es->bypassed = mChain.loaded[i].bypassed;
    }
  }
  writeParam(::kMasterOutput, static_cast<float>(s.master_output_db));

  if (mChain.selectedIndex >= 0) onSlotSelected(mChain.selectedIndex);
}

void ToneView::loadModelIntoSlot(int slotIndex,
                                  const std::string& toneId,
                                  const std::string& modelId)
{
  if (toneId.empty() || modelId.empty()) return;
  auto row = ::t3k::library::LibraryDb::instance()
               .findByToneAndModelId(toneId, modelId);
  if (!row.has_value()) return;

  // Pick the destination slot index. -1 means "first empty pedal slot
  // (0..2)" — wrapping to 0 if none free.
  int dst = slotIndex;
  if (dst < 0) {
    for (int i = kPedalSlotMin; i <= kPedalSlotMax; ++i) {
      bool occupied = false;
      for (const auto& ls : mChain.loaded) {
        if (ls.slotIndex == i) { occupied = true; break; }
      }
      if (!occupied) { dst = i; break; }
    }
    if (dst < 0) dst = kPedalSlotMin;
  }

  // Build the new LoadedSlot from the LibraryDb row.
  ChainView::LoadedSlot ls;
  ls.slotIndex            = dst;
  ls.toneId               = toneId;
  ls.modelId              = modelId;
  if (row->gear_type == "amp")        ls.iconType = GearType::Amp;
  else if (row->gear_type == "cab")   ls.iconType = GearType::Cab;
  else if (row->gear_type == "outboard") ls.iconType = GearType::Outboard;
  else if (row->gear_type == "full-rig") ls.iconType = GearType::FullRig;
  else                                   ls.iconType = GearType::Pedal;
  ls.kind                = row->kind;  // "nam" / "ir" — drives syncDspChain routing.
  ls.info.displayName    = row->effectiveDisplayName();
  ls.info.creator        = row->t3k_creator;
  ls.info.format         = (row->kind == "ir") ? "IR" : "NAM";
  ls.info.sizeBytes      = row->size_bytes;
  ls.info.downloadedAtMs = row->added_at;
  ls.info.description    = row->t3k_description;
  // Phase 10 — cache the on-disk path so syncDspChain doesn't need to
  // re-query LibraryDb when it walks the chain. row->uri is the UTF-8
  // absolute path (see ModelSidecar / Downloader writers).
  ls.absPath             = row->uri;
  // Image source (local sibling preferred; URL falls through to the
  // ThumbnailCache when the slot paints).
  if (row->t3k_image_path.has_value()) ls.imagePath = *row->t3k_image_path;
  ls.imageUrl            = row->t3k_image_url;
  // The info pane on the right of the chain strip shows the same
  // picture when this slot is selected.
  ls.info.imagePath      = ls.imagePath;
  ls.info.imageUrl       = ls.imageUrl;

  // Replace any existing entry at `dst`; otherwise append.
  bool replaced = false;
  for (auto& existing : mChain.loaded) {
    if (existing.slotIndex == dst) {
      existing = ls;
      replaced = true;
      break;
    }
  }
  if (!replaced) mChain.loaded.push_back(std::move(ls));

  mChain.selectedIndex = dst;
  rebuildStrip();
  syncDspChain();
  onSlotSelected(dst);
}

void ToneView::syncDspChain()
{
  // 2026-05-25 refactor: stable DSP slot assignment + chain-order
  // indirection. Each loaded entry is PINNED to its first-assigned
  // ExtraSlot once staged — subsequent reorders only update the
  // processing order via mPlugin.SetChainOrder(), never restage the
  // model. That kills the audio-pop the user reported.
  //
  // Slot 0 (mModel) is reserved for legacy paths and intentionally
  // unused by ToneView — all chain entries go to ExtraSlots 1..N-1,
  // giving us up to kNumChainSlots-1 audible models.
  const int kExtraSlots = kNumChainSlots - 1;

  // 1. Walk loaded entries in visual order so excess entries beyond
  //    the DSP budget are deterministically the last ones to fall
  //    off the audible chain.
  std::vector<ChainView::LoadedSlot*> ordered;
  ordered.reserve(mChain.loaded.size());
  for (auto& ls : mChain.loaded) ordered.push_back(&ls);
  std::sort(ordered.begin(), ordered.end(),
            [](const ChainView::LoadedSlot* a, const ChainView::LoadedSlot* b) {
              return a->slotIndex < b->slotIndex;
            });

  // 2. First pass — invalidate dspSlot assignments whose path changed
  //    on the plugin side (rare; happens after a state-chunk restore).
  //    Mark any extra slots no longer claimed by a loaded entry as
  //    "free" so step 3 can reuse them.
  // 2026-05-26 — compare against the kind-appropriate path getter
  //   (GetSlotIRPath for IR entries, GetSlotNamPath for NAM). Without
  //   this an IR entry's path would never match GetSlotNamPath and we'd
  //   restage it on every syncDspChain — audio dropout on every UI
  //   resize / preset apply.
  bool slotInUse[kNumChainSlots] = { false };
  auto pathForSlot = [this](const ChainView::LoadedSlot* ls) -> std::string {
    return (ls->kind == "ir")
             ? std::string(mPlugin.GetSlotIRPath(ls->dspSlot))
             : std::string(mPlugin.GetSlotNamPath(ls->dspSlot));
  };
  for (auto* ls : ordered) {
    if (ls->dspSlot >= 1 && ls->dspSlot < kNumChainSlots
        && !ls->absPath.empty()
        && pathForSlot(ls) == ls->absPath) {
      slotInUse[ls->dspSlot] = true;
    } else {
      ls->dspSlot = -1;  // needs (re)staging into a free slot
    }
  }

  // 3. Assign unstaged entries to the first free DSP slot. Stage them
  //    into the plugin via the appropriate API for the file kind.
  for (auto* ls : ordered) {
    if (ls->absPath.empty()) {
      ls->dspSlot = -1;
      continue;
    }
    if (ls->dspSlot < 0) {
      // Find a free extra slot (skip 0 — reserved for legacy mModel).
      int free = -1;
      for (int i = 1; i < kNumChainSlots; ++i) {
        if (!slotInUse[i]) { free = i; break; }
      }
      if (free < 0) {
        // Over budget — silent in audio.
        continue;
      }
      WDL_String wp(ls->absPath.c_str());
      // 2026-05-26 — route IRs through StageIRInSlot. Pre-this, every
      // file got fed to StageModelInSlot which then early-rejected
      // anything not ending in .nam → IRs were silent. Now IRs land
      // in the slot's ImpulseResponse and process audio in the chain.
      if (ls->kind == "ir") {
        mPlugin.StageIRInSlot(free, wp);
      } else {
        mPlugin.StageModelInSlot(free, wp);
      }
      ls->dspSlot     = free;
      slotInUse[free] = true;
    }
  }

  // 4. Free any DSP slots that no longer correspond to a loaded entry.
  //    Check both NAM and IR occupancy — either is enough to require
  //    an unload.
  for (int i = 1; i < kNumChainSlots; ++i) {
    if (!slotInUse[i] && (mPlugin.SlotHasModel(i) || mPlugin.SlotHasIR(i))) {
      mPlugin.UnloadSlot(i);
    }
  }

  // 5. Push the new processing order. Walk `ordered` (visual order)
  //    and emit the ExtraSlot indices (dspSlot-1, zero-based) for
  //    each entry that landed in the audible chain.
  int chainOrder[kNumChainSlots] = { 0 };
  int chainOrderLen = 0;
  for (auto* ls : ordered) {
    if (ls->dspSlot < 1 || ls->dspSlot >= kNumChainSlots) continue;
    if (chainOrderLen < kExtraSlots) {
      chainOrder[chainOrderLen++] = ls->dspSlot - 1;
    }
  }
  mPlugin.SetChainOrder(chainOrder, chainOrderLen);
}

}  // namespace t3k::ui
