// T3kPresetOverlay — preset selector dropdown (Phase 2b, v6).
//
// Anchored under the T3kPresetPill in the header. Contains, top to bottom:
//   1. Search field with leading magnifier glyph (filters the list below
//      by lowercased substring; click opens iPlug2's text-entry overlay).
//   2. Scrollable preset list (max ~180px tall). Each row shows a ✓ in
//      front of the active preset; clicking a row fires `onSelect(id)`.
//   3. 1px divider.
//   4. Action row: [Save] (primary kAccent fill) · [Save As…] (outline)
//      · [⋯] (overflow menu, 28-wide).
//
// Background #0c0c0c, 1px #1f1f1f border, kRadiusLg. No drop shadow at
// this iPlug2 revision — see the .cpp note.
//
// The PresetStore wiring (Phase 3+) lives in the parent. For Phase 2b,
// ToneRoot stubs five hard-coded preset rows from the v6 mockup.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "IControl.h"
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::IMouseMod;

struct PresetRow {
  int64_t id;
  std::string name;
  bool active;
  int sort_order = 0;
};

class T3kPresetOverlay : public IControl
{
public:
  explicit T3kPresetOverlay(const IRECT& bounds);

  void setPresets(std::vector<PresetRow> rows);
  void setActiveId(int64_t id);
  const std::vector<PresetRow>& presets() const { return mPresets; }

  // Returns the id of the currently-active preset, or 0 if none.
  int64_t activePresetId() const {
    for (const auto& r : mPresets) if (r.active) return r.id;
    return 0;
  }

  // Callbacks — wired by the parent (ToneRoot).
  std::function<void(int64_t)>            onSelect;
  std::function<void()>                   onSave;
  std::function<void()>                   onSaveAs;
  std::function<void()>                   onMoreMenu;
  std::function<void(const std::vector<int64_t>&)> onReorder;
  std::function<void(const std::string&)> onSearchChanged;
  std::function<void(int64_t /*id*/, const std::string& /*name*/)>
                                          onRenamePreset;
  std::function<void(int64_t /*id*/, const std::string& /*name*/)>
                                          onDeletePreset;

  void openContextMenu(int64_t id, const std::string& name);

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override;
  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override;
  void OnMouseUp(float x, float y, const IMouseMod& mod) override;
  void OnTextEntryCompletion(const char* str, int valIdx) override;

private:
  std::vector<PresetRow> mPresets;
  std::string mSearch;  // lowercased

  // Sub-rects computed from mRECT — kept as helpers so Draw and
  // OnMouseDown stay in sync without caching state.
  IRECT searchRect()    const;
  IRECT listRect()      const;
  IRECT dividerRect()   const;
  IRECT actionRowRect() const;
  IRECT saveBtnRect()   const;
  IRECT saveAsBtnRect() const;
  IRECT moreBtnRect()   const;
  IRECT contextMenuRect() const;
  IRECT contextRenameRect() const;
  IRECT contextDeleteRect() const;

  // Row rect for the `filteredIndex`-th row of the filtered list.
  IRECT listRowRect(int filteredIndex) const;

  // Returns the filtered subset (case-insensitive substring on mSearch).
  std::vector<const PresetRow*> filteredRows() const;

  bool mContextOpen = false;
  int64_t mContextId = 0;
  std::string mContextName;
  IRECT mContextAnchor;
  bool mDraggingPreset = false;
  int64_t mDragPresetId = 0;
};

}  // namespace t3k::ui
