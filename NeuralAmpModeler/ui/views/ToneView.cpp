// ToneView.cpp — see ToneView.h.

#include "ToneView.h"

#include <algorithm>

#include "../theme.h"
#include "../layout.h"
#include "../controls/T3kDragGhost.h"
#include "../controls/T3kSlot.h"
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

// Slot tile dimensions (Decision 47). Scaled to match the bigger strip.
constexpr float kSlotW        = 88.f;
constexpr float kSlotFullRigW = 116.f;
constexpr float kSlotAddW     = 60.f;
constexpr float kSlotH        = 88.f;
constexpr float kSlotGap      = 12.f;

float widthForSlot(GearType t)
{
  return (t == GearType::FullRig) ? kSlotFullRigW : kSlotW;
}

// Category id derived from slotIndex. Pedals share category 0 (slot indices
// 0..4), outboards share category 2 (slot indices 7..11). Amp (5), Cab (6),
// and any FullRig occupying slot 5 are single-position slots and each get
// their own unique category so the same-category check below rules out a
// drop on a different gear type.
//   0 → pedals
//   1 → amp (single)
//   2 → outboards
//   3 → cab (single)
int slotCategory(int slotIndex)
{
  if (slotIndex >= 0 && slotIndex <= 4) return 0;   // pedals
  if (slotIndex == 5)                   return 1;   // amp
  if (slotIndex == 6)                   return 3;   // cab
  if (slotIndex >= 7 && slotIndex <= 11) return 2;  // outboards
  return -1;
}

// Only categories with more than one possible position support reorder.
bool isReorderableCategory(int slotIndex)
{
  const int c = slotCategory(slotIndex);
  return c == 0 || c == 2;
}

}  // namespace

ToneView::ToneView(const IRECT& bounds, NeuralAmpModeler& plugin)
: IControl(bounds), mPlugin(plugin)
{
  seedDemoSnapshot();
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
  for (T3kSlot* s : mSlots) if (s) s->Hide(hide);
  if (mAddTile)    mAddTile   ->Hide(hide);
  if (mDragGhost)  mDragGhost ->Hide(hide);
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
  for (T3kSlot* s : mSlots) {
    if (s) g->RemoveControl(s);
  }
  mSlots.clear();
  if (mAddTile) {
    g->RemoveControl(mAddTile);
    mAddTile = nullptr;
  }
}

void ToneView::computeStripLayout(float& outStartX, float& outTopY) const
{
  // Compute total width so the row can be horizontally centered (matches
  // the mockup's `justify-content: center` on .plg-strip).
  float totalW = 0.f;
  for (const auto& ls : mChain.loaded) {
    totalW += widthForSlot(ls.iconType);
  }
  if (!mChain.loaded.empty()) totalW += kSlotGap * mChain.loaded.size();  // gap after each tile + before Add
  totalW += kSlotAddW;

  outStartX = mStripRect.MW() - totalW * 0.5f;
  outTopY   = mStripRect.MH() - kSlotH * 0.5f;
}

void ToneView::layoutStripTiles()
{
  if (mSlots.empty() && !mAddTile) return;
  // Sanity: mSlots and mChain.loaded should stay in sync (rebuildStrip is
  // the only way they diverge, and rebuildStrip rebuilds both in lockstep).
  if (mSlots.size() != mChain.loaded.size()) return;

  float startX, topY;
  computeStripLayout(startX, topY);
  float x = startX;
  for (size_t i = 0; i < mChain.loaded.size(); ++i) {
    const float w = widthForSlot(mChain.loaded[i].iconType);
    if (mSlots[i]) mSlots[i]->SetTargetAndDrawRECTs(IRECT(x, topY, x + w, topY + kSlotH));
    x += w + kSlotGap;
  }
  if (mAddTile) {
    mAddTile->SetTargetAndDrawRECTs(IRECT(x, topY, x + kSlotAddW, topY + kSlotH));
  }

  // Tile positions changed → recompute the per-tile drag bounds so a
  // window-resize during a drag (rare, but possible) doesn't leak
  // stale clamps.
  updateDragBoundsForCategories();
}

void ToneView::rebuildStrip()
{
  IGraphics* g = GetUI();
  if (!g) return;

  float startX, topY;
  computeStripLayout(startX, topY);

  // Detach existing tiles before laying out fresh ones.
  clearStripChildren();
  mSlots.reserve(mChain.loaded.size());

  float x = startX;
  for (const auto& ls : mChain.loaded) {
    const float w = widthForSlot(ls.iconType);
    const IRECT r(x, topY, x + w, topY + kSlotH);
    const int idx = ls.slotIndex;
    auto* tile = new T3kSlot(
        r, idx, T3kSlot::Variant::Loaded, ls.iconType,
        /*onSelect*/ [this](int slot) { onSlotSelected(slot); },
        /*onRemove*/ [this](int slot) { onSlotRemoved(slot); },
        /*onAdd*/    {});
    tile->setSelected(idx == mChain.selectedIndex);

    // Only pedal / outboard tiles support drag-to-reorder. Amp / Cab /
    // FullRig live at fixed positions, so we leave their drag callbacks
    // null and T3kSlot disables drag accordingly.
    if (isReorderableCategory(idx)) {
      // onDragStart points the drag ghost at this tile so it paints
      // above the strip's siblings. The ghost is attached last in
      // z-order (see recreateDragGhostOnTop) — without this hook the
      // dragged tile would render under tiles attached after it.
      tile->setOnDragStart([this](int /*slot*/) {
        if (mDragGhost) {
          // Find the corresponding tile pointer to hand the ghost.
          // mSlots is parallel to mChain.loaded so the active drag
          // source is always the slot whose mDragging just flipped
          // true — but for simplicity, scan to find the one that
          // reports isDragging(). Cheap (≤5 entries per category).
          for (T3kSlot* s : mSlots) {
            if (s && s->isDragging()) { mDragGhost->setSource(s); break; }
          }
        }
      });
      tile->setOnDragMove([this](int slot, float mx, float my) {
        // The slot itself early-returns in Draw while dragging, so the
        // ghost is the only thing that paints the moving tile. Mark
        // it dirty on every drag tick so iPlug2 schedules a repaint
        // in lock-step with the cursor motion.
        if (mDragGhost) mDragGhost->SetDirty(false);
        onSlotDragMove(slot, mx, my);
      });
      tile->setOnDragEnd([this](int slot, float mx, float my) {
        if (mDragGhost) mDragGhost->clear();
        onSlotDragEnd(slot, mx, my);
      });
    }

    g->AttachControl(tile);
    mSlots.push_back(tile);
    x += w + kSlotGap;
  }

  // Trailing Add tile.
  const IRECT addR(x, topY, x + kSlotAddW, topY + kSlotH);
  mAddTile = new T3kSlot(
      addR, /*slotIndex*/ -1, T3kSlot::Variant::Add, GearType::Pedal,
      /*onSelect*/ {},
      /*onRemove*/ {},
      /*onAdd*/    [this](int /*slot*/) { onSlotAdded(); });
  g->AttachControl(mAddTile);

  // Push the per-tile drag-bounds onto each reorderable slot now that
  // positions exist.
  updateDragBoundsForCategories();

  // Bump the drag ghost to the very end of the IGraphics control list
  // so it paints above the just-attached strip tiles.
  recreateDragGhostOnTop();

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
  // This guarantees the dragged tile's drawn position can never cross
  // the amp/cab boundary or run off the edge of the strip — the
  // visual matches what the drop logic accepts in onSlotDragEnd.
  if (mSlots.size() != mChain.loaded.size()) return;
  for (size_t i = 0; i < mSlots.size(); ++i) {
    if (!mSlots[i]) continue;
    const int slotIdx = mChain.loaded[i].slotIndex;
    if (!isReorderableCategory(slotIdx)) continue;
    const int cat = slotCategory(slotIdx);

    float catLeft  = mSlots[i]->GetRECT().L;
    float catRight = mSlots[i]->GetRECT().R;
    for (size_t j = 0; j < mSlots.size(); ++j) {
      if (!mSlots[j]) continue;
      if (slotCategory(mChain.loaded[j].slotIndex) != cat) continue;
      catLeft  = std::min(catLeft,  mSlots[j]->GetRECT().L);
      catRight = std::max(catRight, mSlots[j]->GetRECT().R);
    }

    const float baseL = mSlots[i]->GetRECT().L;
    const float tileW = mSlots[i]->GetRECT().W();
    // Allowable offsets:
    //   minOffset → tile slid left until its L == catLeft
    //   maxOffset → tile slid right until its R == catRight,
    //               i.e. L == catRight - tileW.
    mSlots[i]->setDragBoundsX(catLeft - baseL,
                              catRight - tileW - baseL);
  }
}

void ToneView::recreateDragGhostOnTop()
{
  IGraphics* g = GetUI();
  if (!g) return;

  // Destroy + recreate to land at the end of IGraphics's flat control
  // list (= top of z-order). Same pattern as ToneRoot uses for the
  // preset overlay. Ghost holds no state — just a non-owning pointer
  // to whichever T3kSlot is currently dragging (cleared on drag-end),
  // so destroying it mid-strip-rebuild is safe.
  if (mDragGhost) {
    g->RemoveControl(mDragGhost);
    mDragGhost = nullptr;
  }
  // Size to the strip area — the actual paint happens at the slot's
  // own offset rect, but iPlug2 uses the control's mRECT for dirty
  // tracking, so spanning the strip captures any plausible drag
  // position.
  mDragGhost = new T3kDragGhost(mStripRect);
  g->AttachControl(mDragGhost);
}

void ToneView::onSlotSelected(int slotIndex)
{
  mChain.selectedIndex = slotIndex;
  for (size_t i = 0; i < mChain.loaded.size(); ++i) {
    const bool sel = (mChain.loaded[i].slotIndex == slotIndex);
    if (i < mSlots.size() && mSlots[i]) mSlots[i]->setSelected(sel);
    if (sel && mInfoPane) mInfoPane->setSnapshot(mChain.loaded[i].info);
  }
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
  rebuildStrip();
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
  // Find the source index in mChain.loaded.
  auto srcIt = std::find_if(mChain.loaded.begin(), mChain.loaded.end(),
                            [slotIndex](const ChainView::LoadedSlot& s) {
                              return s.slotIndex == slotIndex;
                            });
  if (srcIt == mChain.loaded.end()) {
    rebuildStrip();  // safety: re-snap the tile back to its original rect
    return;
  }
  const size_t srcPos = static_cast<size_t>(srcIt - mChain.loaded.begin());
  const int srcCat = slotCategory(slotIndex);

  // Find which tile (by parallel index in mSlots) sits under (x, y).
  // mSlots is sized to mChain.loaded; each tile's mRECT was set by
  // computeStripLayout(). A drop outside any tile is a no-op (we just
  // rebuildStrip to snap the visual back).
  int dstPos = -1;
  for (size_t i = 0; i < mSlots.size(); ++i) {
    if (mSlots[i] && mSlots[i]->GetRECT().Contains(x, y)) {
      dstPos = static_cast<int>(i);
      break;
    }
  }
  if (dstPos < 0) {
    rebuildStrip();
    return;
  }

  // Same source as drop target → no-op (the user just twitched).
  if (static_cast<size_t>(dstPos) == srcPos) {
    rebuildStrip();
    return;
  }

  // Cross-category drops are rejected. The dragged tile snaps back.
  const int dstSlotIdx = mChain.loaded[dstPos].slotIndex;
  if (slotCategory(dstSlotIdx) != srcCat) {
    rebuildStrip();
    return;
  }

  // Reorder by moving the source entry to the destination position. After
  // erasing srcPos, dstPos shifts left by one if it sat past srcPos; the
  // adjustedDst below compensates so the moved entry lands at the visual
  // position the user dropped on. We also rotate the slotIndex values so
  // the chain DSP order stays consistent with the visual order — the
  // slotIndex is what audio code will use to look up effects in chain
  // position once Phase 3 wires it.
  ChainView::LoadedSlot moved = std::move(mChain.loaded[srcPos]);
  mChain.loaded.erase(mChain.loaded.begin() + srcPos);
  const size_t adjustedDst = (static_cast<size_t>(dstPos) > srcPos)
                                 ? static_cast<size_t>(dstPos - 1)
                                 : static_cast<size_t>(dstPos);
  mChain.loaded.insert(mChain.loaded.begin() + adjustedDst, std::move(moved));

  // Re-number slotIndices within the source category so they stay
  // contiguous and reflect the new visual order. Pedals get 0..4,
  // outboards 7..11. Other categories don't reorder so they're
  // untouched.
  int nextIdx = (srcCat == 0) ? 0 : 7;
  for (auto& ls : mChain.loaded) {
    if (slotCategory(ls.slotIndex) == srcCat) {
      ls.slotIndex = nextIdx++;
    }
  }
  // Keep the selection on the just-moved item.
  mChain.selectedIndex = mChain.loaded[adjustedDst].slotIndex;

  rebuildStrip();
  if (mChain.selectedIndex >= 0) onSlotSelected(mChain.selectedIndex);
}

void ToneView::onSlotAdded()
{
  // Phase 2b stub — append a hard-coded sample model. Phase 3 replaces
  // this with a real model-picker overlay.
  ChainView::LoadedSlot sample;
  int nextIdx = 0;
  for (const auto& ls : mChain.loaded) nextIdx = std::max(nextIdx, ls.slotIndex + 1);
  sample.slotIndex = nextIdx;
  sample.iconType  = GearType::Pedal;
  sample.toneId    = "sample-tone";
  sample.modelId   = "sample-model";
  sample.info.displayName = "Sample Pedal";
  sample.info.creator     = "tone3000";
  sample.info.format      = "NAM";
  sample.info.sizeBytes   = 8 * 1024 * 1024;
  sample.info.tags        = { "sample", "phase-2b" };
  sample.info.description = "Phase 2b stub model. Real picker lands in Phase 3.";
  mChain.loaded.push_back(std::move(sample));
  rebuildStrip();
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
    ls.info.displayName    = row->effectiveDisplayName();
    ls.info.creator        = row->t3k_creator;
    ls.info.format         = (row->kind == "ir") ? "IR" : "NAM";
    ls.info.sizeBytes      = row->size_bytes;
    ls.info.downloadedAtMs = row->added_at;
    ls.info.description    = row->t3k_description;
    mChain.loaded.push_back(std::move(ls));
  }
  // If the preset was empty (e.g. fresh "Default Setting"), fall back
  // to the demo seed so the UI isn't blank during smoke tests.
  if (mChain.loaded.empty()) {
    seedDemoSnapshot();
  }

  rebuildStrip();
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
  // (0..4)" — wrapping to 0 if none free.
  int dst = slotIndex;
  if (dst < 0) {
    for (int i = 0; i <= 4; ++i) {
      bool occupied = false;
      for (const auto& ls : mChain.loaded) {
        if (ls.slotIndex == i) { occupied = true; break; }
      }
      if (!occupied) { dst = i; break; }
    }
    if (dst < 0) dst = 0;
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
  ls.info.displayName    = row->effectiveDisplayName();
  ls.info.creator        = row->t3k_creator;
  ls.info.format         = (row->kind == "ir") ? "IR" : "NAM";
  ls.info.sizeBytes      = row->size_bytes;
  ls.info.downloadedAtMs = row->added_at;
  ls.info.description    = row->t3k_description;

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
  onSlotSelected(dst);
}

}  // namespace t3k::ui
