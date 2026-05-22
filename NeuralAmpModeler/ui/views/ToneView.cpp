// ToneView.cpp — see ToneView.h.

#include "ToneView.h"

#include <algorithm>

#include "../theme.h"
#include "../layout.h"
#include "../controls/T3kSlot.h"
#include "../controls/T3kKnob.h"

// Plug-in header — gives us the EParams enum (kInputLevel, kToneBass, ...).
#include "../../NeuralAmpModeler.h"

namespace t3k::ui {

using namespace iplug::igraphics;

namespace {

// Section heights (match the v6 mockup .plg-strip / .plg-info / .plg-knobs).
constexpr float kStripH = 96.f;
constexpr float kKnobH  = 78.f;

// Slot tile dimensions (Decision 47).
constexpr float kSlotW        = 64.f;
constexpr float kSlotFullRigW = 82.f;
constexpr float kSlotAddW     = 44.f;
constexpr float kSlotH        = 64.f;
constexpr float kSlotGap      = 8.f;

float widthForSlot(GearType t)
{
  return (t == GearType::FullRig) ? kSlotFullRigW : kSlotW;
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

  // The rebuild appended new tiles to the end of the IGraphics control
  // list. If anything was attached later (e.g. the preset overlay), that
  // control would now sit BELOW the strip in z-order. Notify any listener
  // (ToneRoot, currently) so it can re-promote overlays that should stay
  // on top.
  if (mOnStripRebuilt) mOnStripRebuilt();
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

}  // namespace t3k::ui
