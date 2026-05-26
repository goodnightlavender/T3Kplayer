// ToneView.cpp — see ToneView.h.

#include "ToneView.h"

#include <algorithm>

#include "../theme.h"
#include "../layout.h"
#include "../controls/T3kModelTile.h"
#include "../controls/T3kKnob.h"

// Plug-in header — gives us the EParams enum (kInputLevel, kToneBass, ...).
#include "../../NeuralAmpModeler.h"

#include "../../library/LibraryDb.h"
#include "../../library/PresetState.h"

namespace t3k::ui {

using namespace iplug::igraphics;

namespace {

// Section heights (match the v6 mockup .plg-strip / .plg-info / .plg-knobs).
// Scaled ~×1.36 for the 1280×800 default window — without this the strip and
// knob row hugged the top and bottom of a tall body, leaving an oversized
// empty middle where the info pane couldn't fill the space.
constexpr float kStripH = 132.f;
constexpr float kKnobH  = 108.f;

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
  // Carve the body into: top strip / middle info / bottom knob row.
  auto topSplit    = t3k::layout::rowFixedTop(mRECT, kStripH);
  mStripRect       = topSplit.first;
  IRECT remainder  = topSplit.second;

  auto bottomSplit = t3k::layout::rowFixedBottom(remainder, kKnobH);
  mInfoRect        = bottomSplit.first;
  mKnobRect        = bottomSplit.second;

  if (mInfoPane) mInfoPane->SetTargetAndDrawRECTs(mInfoRect);

  // Resize strip tiles in place — do NOT detach/reattach them. Earlier
  // versions called rebuildStrip() here, which removed all existing tiles
  // and re-added them. Because iPlug2 appends new controls to the end of
  // mControls, that re-add ran AFTER the preset overlay had already been
  // attached (ToneRoot::OnAttached ends with OnResize() to size children),
  // which pushed the tiles in front of the overlay in z-order — and the
  // overlay then drew under the strip. Updating positions in place keeps
  // existing z-order intact.
  layoutStripTiles();

  if (mKnobIn || mKnobBass || mKnobMid || mKnobTreble || mKnobOut) {
    auto cells = t3k::layout::row(
        t3k::layout::pad(mKnobRect, t3k::theme::kS2, t3k::theme::kS5,
                                    t3k::theme::kS2, t3k::theme::kS5),
        { 1.f, 1.f, 1.f, 1.f, 1.f }, t3k::theme::kS4);
    if (mKnobIn)     mKnobIn->SetTargetAndDrawRECTs(cells[0]);
    if (mKnobBass)   mKnobBass->SetTargetAndDrawRECTs(cells[1]);
    if (mKnobMid)    mKnobMid->SetTargetAndDrawRECTs(cells[2]);
    if (mKnobTreble) mKnobTreble->SetTargetAndDrawRECTs(cells[3]);
    if (mKnobOut)    mKnobOut->SetTargetAndDrawRECTs(cells[4]);
  }
}

void ToneView::OnAttached()
{
  IGraphics* g = GetUI();
  if (!g) return;

  // Info pane — created once, lives for ToneView's lifetime.
  mInfoPane = new T3kModelInfoPane(mInfoRect);
  g->AttachControl(mInfoPane);

  // Knob row — 5 persistent knobs bound to upstream EParams indices.
  auto cells = t3k::layout::row(
      t3k::layout::pad(mKnobRect, t3k::theme::kS2, t3k::theme::kS5,
                                  t3k::theme::kS2, t3k::theme::kS5),
      { 1.f, 1.f, 1.f, 1.f, 1.f }, t3k::theme::kS4);
  mKnobIn     = new T3kKnob(cells[0], ::kInputLevel,  "INPUT");
  mKnobBass   = new T3kKnob(cells[1], ::kToneBass,    "BASS");
  mKnobMid    = new T3kKnob(cells[2], ::kToneMid,     "MID");
  mKnobTreble = new T3kKnob(cells[3], ::kToneTreble,  "TREBLE");
  mKnobOut    = new T3kKnob(cells[4], ::kOutputLevel, "OUTPUT");
  g->AttachControl(mKnobIn);
  g->AttachControl(mKnobBass);
  g->AttachControl(mKnobMid);
  g->AttachControl(mKnobTreble);
  g->AttachControl(mKnobOut);

  // Strip tiles — created from the demo seed.
  rebuildStrip();

  // Reflect initial selection in the info pane.
  if (mChain.selectedIndex >= 0) {
    onSlotSelected(mChain.selectedIndex);
  }
}

void ToneView::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Section separator above the knob row (matches the v6 .plg-knobs
  // border-top: 1px solid #141414).
  g.FillRect(th::kBorder,
             IRECT(mRECT.L, mKnobRect.T - 1.f, mRECT.R, mKnobRect.T));
}

void ToneView::Hide(bool hide)
{
  IControl::Hide(hide);
  // iPlug2 attaches all controls flat — hiding this view does NOT
  // auto-propagate. Cascade to every child so the slot strip / info
  // pane / knob row don't leak onto Library or Cloud tabs.
  for (T3kModelTile* t : mTiles) if (t) t->Hide(hide);
  if (mInfoPane)   mInfoPane  ->Hide(hide);
  if (mKnobIn)     mKnobIn    ->Hide(hide);
  if (mKnobBass)   mKnobBass  ->Hide(hide);
  if (mKnobMid)    mKnobMid   ->Hide(hide);
  if (mKnobTreble) mKnobTreble->Hide(hide);
  if (mKnobOut)    mKnobOut   ->Hide(hide);
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
  // Place tiles using fixed equal width derived from mStripRect. The strip
  // always holds kNumChainSlots tiles arranged in three groups (3 pedals,
  // amp+cab, 3 outboard). The master-knob bookend on the right takes the
  // last 64 px of the strip (Phase E3); for now, leave that space empty.
  //
  // tileW math: 8 tiles, 5 within-group gaps, 2 between-group gaps.
  const float availW = mStripRect.W();
  const float tileH  = std::min(mStripRect.H() - 4.f, 84.f);
  const float tileW  =
      (availW - 2.f * kGroupGap - 5.f * kTileGapWithinGroup) / 8.f;

  outStartX = mStripRect.L;  // left-anchored; master knob will eat the right
  outTopY   = mStripRect.MH() - tileH * 0.5f;
  (void)tileW;  // outTopY/X are the only outputs; tile placement does the rest
}

void ToneView::layoutStripTiles()
{
  if (mTiles.empty()) return;
  if (static_cast<int>(mTiles.size()) != ::kNumChainSlots) return;

  const float availW = mStripRect.W();
  const float tileH  = std::min(mStripRect.H() - 4.f, 84.f);
  const float tileW  =
      (availW - 2.f * kGroupGap - 5.f * kTileGapWithinGroup) / 8.f;
  const float topY   = mStripRect.MH() - tileH * 0.5f;

  // Walk the 8 slots in chain order, inserting a kGroupGap when we cross
  // pedals→amp/cab (idx 2→3) and amp/cab→outboard (idx 4→5).
  float x = mStripRect.L;
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
  // Find the loaded entry corresponding to this slot and push its info /
  // active-slot pointer through to the inspector + DSP.
  auto it = std::find_if(
      mChain.loaded.begin(), mChain.loaded.end(),
      [slotIndex](const ChainView::LoadedSlot& s) {
        return s.slotIndex == slotIndex;
      });
  if (it != mChain.loaded.end()) {
    if (mInfoPane) mInfoPane->setSnapshot(it->info);
    if (it->dspSlot >= 0) mPlugin.SetActiveSlot(it->dspSlot);
  } else if (mInfoPane) {
    mInfoPane->clear();
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

void ToneView::onSlotRemoved(int slotIndex)
{
  auto it = std::find_if(mChain.loaded.begin(), mChain.loaded.end(),
                         [slotIndex](const ChainView::LoadedSlot& s) {
                           return s.slotIndex == slotIndex;
                         });
  if (it == mChain.loaded.end()) return;
  const bool wasSelected = (mChain.selectedIndex == slotIndex);
  mChain.loaded.erase(it);

  if (wasSelected) {
    mChain.selectedIndex = mChain.loaded.empty() ? -1
                                                 : mChain.loaded.front().slotIndex;
    if (mChain.selectedIndex < 0 && mInfoPane) {
      mInfoPane->clear();
    }
  }
  // Removed entry: syncDspChain detects it (its slot is no longer in
  // mChain.loaded) and unloads the corresponding DSP slot. Remaining
  // models stay pinned to their existing dspSlot — no restage, no
  // audio dropout. The processing order updates via SetChainOrder.
  rebuildStrip();
  syncDspChain();
  if (mChain.selectedIndex >= 0) onSlotSelected(mChain.selectedIndex);
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

void ToneView::seedDemoSnapshot()
{
  // Stable demo timestamp — T3kModelInfoPane offsets this by +14d internally
  // so the meta line deterministically reads "downloaded 2 weeks ago".
  constexpr int64_t kDemoDownloadAt = 1700000000LL * 1000LL;

  auto mk = [&](int slotIdx, GearType type, const std::string& tone,
                const std::string& model, const std::string& name,
                const std::string& creator, int64_t bytes,
                std::vector<std::string> tags, const std::string& desc)
  {
    ChainView::LoadedSlot s;
    s.slotIndex = slotIdx;
    s.iconType  = type;
    s.toneId    = tone;
    s.modelId   = model;
    s.info.displayName    = name;
    s.info.creator        = creator;
    s.info.format         = "NAM";
    s.info.sizeBytes      = bytes;
    s.info.downloadedAtMs = kDemoDownloadAt;
    s.info.tags           = std::move(tags);
    s.info.description    = desc;
    return s;
  };

  mChain.loaded.push_back(mk(0, GearType::Pedal,
      "ts808-tone", "ts808-model",
      "Tube Screamer", "tone3000",
      9 * 1024 * 1024, {"overdrive", "mid-hump"},
      "Classic mid-hump overdrive. Pushes the front end of an amp into smooth breakup without losing pick attack."));

  mChain.loaded.push_back(mk(1, GearType::Pedal,
      "klon94-tone", "klon94-model",
      "Klon '94 — original-circuit centaur clone", "primalnerd",
      14 * 1024 * 1024 + 300 * 1024, {"overdrive", "transparent", "klon-clone"},
      "Captured at unity, mid drive, treble at 12 o'clock. Sweetens the front end of a clean amp without losing low-end. Works with single-coils and humbuckers."));

  mChain.loaded.push_back(mk(2, GearType::Pedal,
      "bigmuff-tone", "bigmuff-model",
      "Big Muff", "tone3000",
      11 * 1024 * 1024, {"fuzz", "sustain"},
      "Thick, sustaining fuzz. Wall-of-sound textures for solos and stoner-rock chord work."));

  // Slot index 5 = Amp position in the chain. A Full Rig occupies it and
  // omits the Cab tile (index 6) per Decisions 40, 47.
  mChain.loaded.push_back(mk(5, GearType::FullRig,
      "giltone-fullrig-tone", "giltone-fullrig-model",
      "giltone Full Rig — 5e3 Tweed Deluxe clone", "giltone",
      82 * 1024 * 1024, {"full-rig", "tweed", "amp+cab"},
      "Full rig capture: 15W tweed Deluxe clone into a stock pine cab, mic'd with a single SM57. Honky and aggressive."));

  mChain.loaded.push_back(mk(7, GearType::Outboard,
      "ssl-bus-tone", "ssl-bus-model",
      "SSL Bus Compressor", "tone3000",
      6 * 1024 * 1024, {"outboard", "compressor"},
      "Glues the chain together. Light 2-3 dB of gain reduction on a slow attack adds punch without squashing."));

  // Klon is the demo-selected slot in the v6 mockup.
  mChain.selectedIndex = 1;
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
