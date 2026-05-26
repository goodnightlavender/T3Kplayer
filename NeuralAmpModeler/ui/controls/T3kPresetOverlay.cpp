// T3kPresetOverlay implementation. See T3kPresetOverlay.h.

#include "T3kPresetOverlay.h"

#include <algorithm>
#include <cctype>

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// Layout constants tracking the v6 mockup (.plg-preset-overlay et al).
constexpr float kPanelPad    = 8.f;   // outer inset for the search/list/buttons
constexpr float kSearchH     = 26.f;
constexpr float kSearchPadL  = 26.f;  // glyph + gap before the placeholder text
constexpr float kRowH        = 22.f;
constexpr float kListMaxH    = 180.f;
constexpr float kDividerVPad = 4.f;
constexpr float kActionRowH  = 24.f;
constexpr float kActionGap   = 5.f;
constexpr float kMoreBtnW    = 0.f;
constexpr float kContextW    = 124.f;
constexpr float kContextRowH = 24.f;

// Panel chrome colors — pulled from the v6 mockup, not theme tokens, because
// the overlay sits slightly darker than the rest of the surface palette.
const IColor kPanelBg    {255,  12,  12,  12};  // #0c0c0c
const IColor kPanelBorder{255,  31,  31,  31};  // #1f1f1f
const IColor kRowHoverBg {255,  21,  21,  21};  // #151515
const IColor kSearchBg   {255,   5,   5,   5};  // #050505

// Lower-case an ASCII string (good enough for the demo preset set).
std::string Lower(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return out;
}

// Tiny magnifier glyph — mirrors T3kSearchBar's helper but offsets within
// the overlay's search field.
void DrawMagnifier(IGraphics& g, float cx, float cy, const IColor& col)
{
  const float r = 4.5f;
  g.DrawCircle(col, cx, cy, r, nullptr, 1.5f);
  const float h = r * 0.707f;
  g.DrawLine(col, cx + h, cy + h, cx + h + 3.5f, cy + h + 3.5f, nullptr, 1.5f);
}

}  // namespace

T3kPresetOverlay::T3kPresetOverlay(const IRECT& bounds)
: IControl(bounds)
{
}

void T3kPresetOverlay::setPresets(std::vector<PresetRow> rows)
{
  mPresets = std::move(rows);
  SetDirty(false);
}

void T3kPresetOverlay::setActiveId(int64_t id)
{
  for (auto& r : mPresets) r.active = (r.id == id);
  SetDirty(false);
}

void T3kPresetOverlay::openContextMenu(int64_t id, const std::string& name)
{
  if (id <= 0) return;
  mContextOpen = true;
  mContextId = id;
  mContextName = name;
  mContextAnchor = moreBtnRect();
  SetDirty(false);
}

IRECT T3kPresetOverlay::searchRect() const
{
  return IRECT(mRECT.L + kPanelPad,
               mRECT.T + kPanelPad,
               mRECT.R - kPanelPad,
               mRECT.T + kPanelPad + kSearchH);
}

IRECT T3kPresetOverlay::listRect() const
{
  const IRECT s = searchRect();
  const float top = s.B + kPanelPad * 0.5f;
  // Available height between the search field and the action row.
  const float actionTop = actionRowRect().T;
  const float dividerH  = 1.f + kDividerVPad * 2.f;
  float bottom = actionTop - dividerH;
  if (bottom - top > kListMaxH) bottom = top + kListMaxH;
  return IRECT(mRECT.L + kPanelPad, top, mRECT.R - kPanelPad, bottom);
}

IRECT T3kPresetOverlay::dividerRect() const
{
  const IRECT a = actionRowRect();
  return IRECT(mRECT.L + kPanelPad,
               a.T - kDividerVPad - 1.f,
               mRECT.R - kPanelPad,
               a.T - kDividerVPad);
}

IRECT T3kPresetOverlay::actionRowRect() const
{
  return IRECT(mRECT.L + kPanelPad,
               mRECT.B - kPanelPad - kActionRowH,
               mRECT.R - kPanelPad,
               mRECT.B - kPanelPad);
}

IRECT T3kPresetOverlay::saveBtnRect() const
{
  return IRECT();
}

IRECT T3kPresetOverlay::saveAsBtnRect() const
{
  return actionRowRect();
}

IRECT T3kPresetOverlay::moreBtnRect() const
{
  const IRECT a = actionRowRect();
  return IRECT(a.R - kMoreBtnW, a.T, a.R, a.B);
}

IRECT T3kPresetOverlay::contextMenuRect() const
{
  IRECT anchor = mContextAnchor.W() > 0.f ? mContextAnchor : moreBtnRect();
  float left = anchor.R - kContextW;
  if (left < mRECT.L + kPanelPad) left = mRECT.L + kPanelPad;
  float top = anchor.T - kContextRowH * 2.f - 4.f;
  if (top < mRECT.T + kPanelPad) top = anchor.B + 4.f;
  top = anchor.B + 4.f;
  return IRECT(left, top, left + kContextW, top + kContextRowH * 2.f);
}

IRECT T3kPresetOverlay::contextRenameRect() const
{
  const IRECT r = contextMenuRect();
  return IRECT(r.L, r.T, r.R, r.T + kContextRowH);
}

IRECT T3kPresetOverlay::contextDeleteRect() const
{
  const IRECT r = contextMenuRect();
  return IRECT(r.L, r.T + kContextRowH, r.R, r.B);
}

IRECT T3kPresetOverlay::listRowRect(int filteredIndex) const
{
  const IRECT lr = listRect();
  const float top = lr.T + filteredIndex * kRowH;
  return IRECT(lr.L, top, lr.R, top + kRowH);
}

std::vector<const PresetRow*> T3kPresetOverlay::filteredRows() const
{
  std::vector<const PresetRow*> out;
  out.reserve(mPresets.size());
  if (mSearch.empty()) {
    for (const auto& r : mPresets) out.push_back(&r);
    return out;
  }
  for (const auto& r : mPresets) {
    if (Lower(r.name).find(mSearch) != std::string::npos)
      out.push_back(&r);
  }
  return out;
}

void T3kPresetOverlay::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Panel chrome. iPlug2/NanoVG in this revision doesn't expose a cheap
  // drop-shadow primitive — we ship without one. The 1px panel border
  // still reads the overlay as a distinct surface against the black header.
  g.FillRoundRect(kPanelBg, mRECT, th::kRadiusLg);
  g.DrawRoundRect(kPanelBorder, mRECT, th::kRadiusLg, nullptr, 1.f);

  // ── Search field ────────────────────────────────────────────────────
  const IRECT sr = searchRect();
  g.FillRoundRect(kSearchBg, sr, th::kRadiusSm);
  g.DrawRoundRect(kPanelBorder, sr, th::kRadiusSm, nullptr, 1.f);
  DrawMagnifier(g, sr.L + 13.f, sr.MH(), th::kTextDim);

  const IRECT searchText(sr.L + kSearchPadL, sr.T, sr.R - 8.f, sr.B);
  const bool empty = mSearch.empty();
  const IText searchLabel(th::kTypeSmall,
                          empty ? th::kTextMuted : th::kText,
                          th::kFontBody,
                          EAlign::Near,
                          EVAlign::Middle);
  g.DrawText(searchLabel,
             empty ? "Search presets\xE2\x80\xA6" : mSearch.c_str(),
             searchText);

  // ── Preset list ─────────────────────────────────────────────────────
  const auto rows = filteredRows();
  for (size_t i = 0; i < rows.size(); ++i) {
    const PresetRow& row = *rows[i];
    const IRECT rr = listRowRect(static_cast<int>(i));
    if (rr.B > listRect().B) break;  // simple clip — no scroll yet

    if (row.active)
      g.FillRoundRect(kRowHoverBg, rr, th::kRadiusSm);

    // ✓ glyph at the left for the active preset.
    const IRECT checkRect(rr.L + 4.f, rr.T, rr.L + 20.f, rr.B);
    if (row.active) {
      const IText check(th::kTypeLabel,
                        th::kAccent,
                        th::kFontBodyBold,
                        EAlign::Center,
                        EVAlign::Middle);
      g.DrawText(check, "\xE2\x9C\x93", checkRect);  // U+2713
    }

    // Name to the right of the check column.
    const IRECT nameRect(checkRect.R + 4.f, rr.T, rr.R - 6.f, rr.B);
    const IText name(th::kTypeSmall,
                     row.active ? th::kText : th::kTextMuted,
                     row.active ? th::kFontBodyMed : th::kFontBody,
                     EAlign::Near,
                     EVAlign::Middle);
    g.DrawText(name, row.name.c_str(), nameRect);
  }

  // ── Divider ─────────────────────────────────────────────────────────
  g.FillRect(kPanelBorder, dividerRect());

  // ── Action row ──────────────────────────────────────────────────────
  // Save.
  // Save As… (outline).
  const IRECT saveAs = saveAsBtnRect();
  g.DrawRoundRect(kPanelBorder, saveAs, th::kRadiusSm, nullptr, 1.f);
  g.DrawText(IText(th::kTypeLabel, th::kText, th::kFontBodySemi,
                   EAlign::Center, EVAlign::Middle),
             "Save Preset", saveAs);

  // ⋯ overflow.
  if (mContextOpen) {
    const IRECT menu = contextMenuRect();
    g.FillRoundRect(kPanelBg, menu, th::kRadiusSm);
    g.DrawRoundRect(kPanelBorder, menu, th::kRadiusSm, nullptr, 1.f);
    const IRECT renameText(contextRenameRect().L + 8.f, contextRenameRect().T,
                           contextRenameRect().R - 8.f, contextRenameRect().B);
    const IRECT deleteText(contextDeleteRect().L + 8.f, contextDeleteRect().T,
                           contextDeleteRect().R - 8.f, contextDeleteRect().B);
    g.DrawText(IText(th::kTypeSmall, th::kText, th::kFontBody,
                     EAlign::Near, EVAlign::Middle),
               "Rename", renameText);
    g.DrawText(IText(th::kTypeSmall, th::kText, th::kFontBody,
                     EAlign::Near, EVAlign::Middle),
               "Delete", deleteText);
  }
}

void T3kPresetOverlay::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  namespace th = ::t3k::theme;

  if (mContextOpen) {
    if (contextRenameRect().Contains(x, y)) {
      mContextOpen = false;
      if (onRenamePreset) onRenamePreset(mContextId, mContextName);
      SetDirty(false);
      return;
    }
    if (contextDeleteRect().Contains(x, y)) {
      mContextOpen = false;
      if (onDeletePreset) onDeletePreset(mContextId, mContextName);
      SetDirty(false);
      return;
    }
    mContextOpen = false;
  }

  // Right-click on a preset row → open the custom Rename / Delete menu.
  // We do this
  // BEFORE the regular left-click row handling so a right-click
  // doesn't also select the row.
  if (mod.R) {
    const auto rows = filteredRows();
    for (size_t i = 0; i < rows.size(); ++i) {
      const IRECT rr = listRowRect(static_cast<int>(i));
      if (rr.Contains(x, y)) {
        mContextOpen = true;
        mContextId = rows[i]->id;
        mContextName = rows[i]->name;
        mContextAnchor = rr;
        SetDirty(false);
        return;
      }
    }
    // Right-click outside any row — swallow without action.
    return;
  }

  // Search field → open text entry.
  if (searchRect().Contains(x, y)) {
    if (auto* ui = GetUI()) {
      // Override the text-entry bg/fg so the system field doesn't flash
      // white over our dark overlay surface.
      const IText entryText = IText(th::kTypeSmall,
                                    th::kText,
                                    th::kFontBody,
                                    EAlign::Near,
                                    EVAlign::Middle)
                                  .WithTEColors(th::kBgSurface, th::kText);
      const IRECT sr = searchRect();
      ui->CreateTextEntry(*this, entryText,
                          IRECT(sr.L + kSearchPadL, sr.T, sr.R - 8.f, sr.B),
                          mSearch.c_str());
    }
    SetDirty(false);
    return;
  }

  // Action row buttons (check before list rows so a button click below the
  // list doesn't trigger a phantom row select).
  if (saveAsBtnRect().Contains(x, y)) { if (onSaveAs)   onSaveAs();   SetDirty(false); return; }

  // Preset list rows. Single-click only selects visually; load requires
  // double-click so accidental clicks do not replace the current chain.
  const auto rows = filteredRows();
  for (size_t i = 0; i < rows.size(); ++i) {
    if (listRowRect(static_cast<int>(i)).Contains(x, y)) {
      mDragPresetId = rows[i]->id;
      SetDirty(false);
      return;
    }
  }
}

void T3kPresetOverlay::OnMouseDblClick(float x, float y, const IMouseMod& /*mod*/)
{
  const auto rows = filteredRows();
  for (size_t i = 0; i < rows.size(); ++i) {
    if (listRowRect(static_cast<int>(i)).Contains(x, y)) {
      const int64_t id = rows[i]->id;
      setActiveId(id);
      if (onSelect) onSelect(id);
      SetDirty(false);
      return;
    }
  }
}

void T3kPresetOverlay::OnMouseDrag(float x, float y, float, float, const IMouseMod&)
{
  if (!mSearch.empty() || mDragPresetId <= 0 || mDragPresetId == 1) return;
  const auto rows = filteredRows();
  int target = -1;
  for (size_t i = 0; i < rows.size(); ++i) {
    if (listRowRect(static_cast<int>(i)).Contains(x, y)) {
      target = static_cast<int>(i);
      break;
    }
  }
  if (target <= 0) return;
  const int64_t targetId = rows[static_cast<size_t>(target)]->id;
  auto fromIt = std::find_if(mPresets.begin(), mPresets.end(),
                             [this](const PresetRow& r) { return r.id == mDragPresetId; });
  if (fromIt == mPresets.end()) return;
  const PresetRow moving = *fromIt;
  auto toIt = std::find_if(mPresets.begin(), mPresets.end(),
                           [targetId](const PresetRow& r) { return r.id == targetId; });
  if (toIt == mPresets.end() || fromIt == toIt || toIt->id == 1) return;
  mPresets.erase(fromIt);
  toIt = std::find_if(mPresets.begin(), mPresets.end(),
                      [targetId](const PresetRow& r) { return r.id == targetId; });
  if (toIt == mPresets.end()) mPresets.push_back(moving);
  else mPresets.insert(toIt, moving);
  mDraggingPreset = true;
  SetDirty(false);
}

void T3kPresetOverlay::OnMouseUp(float, float, const IMouseMod&)
{
  if (!mDraggingPreset) {
    mDragPresetId = 0;
    return;
  }
  mDraggingPreset = false;
  mDragPresetId = 0;
  std::vector<int64_t> ids;
  ids.reserve(mPresets.size());
  for (const auto& r : mPresets) ids.push_back(r.id);
  if (onReorder) onReorder(ids);
  SetDirty(false);
}

void T3kPresetOverlay::OnTextEntryCompletion(const char* str, int /*valIdx*/)
{
  mSearch = Lower(str ? str : "");
  if (onSearchChanged) onSearchChanged(mSearch);
  SetDirty(false);
}

}  // namespace t3k::ui
