// T3kModelTile — chain-strip tile (replaces T3kSlot).
//
// Variants: Loaded (icon + name + 6 mono numerals) and Empty (dashed +).
// Loaded supports click-to-focus, drag-to-reorder, double-click to
// toggle bypass. Empty supports click-to-add. Bypassed loaded tiles
// render at ~50% opacity.
//
// Numerals: row 1 = bass / mid / treble, row 2 = inDb / outDb / dryWet.
// All grey.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include "IControl.h"
#include "../theme.h"
#include "T3kGearIcon.h"

namespace t3k::ui {

class T3kModelTile : public iplug::igraphics::IControl
{
public:
  enum class Variant { Loaded, Empty };

  struct Values {
    int bass = 0, mid = 0, treble = 0;
    int inDb = 0, outDb = 0, dryWet = 100;
  };

  T3kModelTile(const iplug::igraphics::IRECT& bounds,
               int slotIndex,
               Variant variant,
               GearType iconType,
               std::function<void(int)> onSelect,
               std::function<void(int)> onAdd,
               std::function<void(int)> onBypassToggle);

  void setVariant(Variant v);
  void setIconType(GearType t);
  void setName(std::string n);
  void setValues(Values v);
  void setSelected(bool s);
  void setBypassed(bool b);

  void setOnDragStart(std::function<void(int)> cb)               { mOnDragStart = std::move(cb); }
  void setOnDragMove (std::function<void(int, float, float)> cb) { mOnDragMove  = std::move(cb); }
  void setOnDragEnd  (std::function<void(int, float, float)> cb) { mOnDragEnd   = std::move(cb); }
  void setDragBoundsX(float minOffsetX, float maxOffsetX);

  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) override;
  void OnMouseDblClick(float x, float y, const iplug::igraphics::IMouseMod& mod) override;
  void OnMouseDrag(float x, float y, float dX, float dY, const iplug::igraphics::IMouseMod& mod) override;
  void OnMouseUp(float x, float y, const iplug::igraphics::IMouseMod& mod) override;

private:
  int      mSlotIndex;
  Variant  mVariant;
  GearType mIconType;
  std::string mName;
  Values   mValues;
  bool     mSelected = false;
  bool     mBypassed = false;

  std::function<void(int)> mOnSelect;
  std::function<void(int)> mOnAdd;
  std::function<void(int)> mOnBypassToggle;
  std::function<void(int)>               mOnDragStart;
  std::function<void(int, float, float)> mOnDragMove;
  std::function<void(int, float, float)> mOnDragEnd;

  bool  mDragging      = false;
  float mDragOffsetX   = 0.f;
  float mDragMinOffset = -1e6f;
  float mDragMaxOffset =  1e6f;

  std::optional<iplug::igraphics::ISVG> mIconSvg;
};

}  // namespace t3k::ui
