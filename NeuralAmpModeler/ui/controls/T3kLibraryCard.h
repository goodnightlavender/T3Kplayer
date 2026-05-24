// T3kLibraryCard — compact vertical tile rendered inside the Library
// tab's 6xN grid. Stripped-down cousin of T3kCard:
//
//   +---------------------+
//   |                     |
//   |   [   gear icon  ]  |   72px gear-icon hero (SVG, tinted to
//   |                     |   match the row's selection state)
//   |                     |
//   |   Display name      |
//   |   Creator . NAM     |
//   +---------------------+
//
// Visual idioms (kBgSurface fill, kBorder/kBorderActive frame, kAccent
// selected ring) match T3kCard so the two surfaces feel like the same
// product. No download pill / player strip — those don't apply to a
// local-library card.
//
// Selection lives outside the card (LibraryView::mSelectedId). The card
// re-paints on setSelected() and dispatches to a single onClick handler;
// the right-click -> popup menu flow stays on LibraryView itself.

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "IControl.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::IMouseMod;

class T3kLibraryCard : public IControl
{
public:
  struct CardData {
    int64_t      id = 0;          // LibraryDb row id (used by selection)
    std::string  displayName;
    std::string  creator;
    std::string  gearType;        // pedal / amp / cab / outboard / full-rig
    std::string  format;          // "NAM" or "IR"
  };

  T3kLibraryCard(const IRECT& bounds,
                 CardData data,
                 std::function<void(int64_t id)> onClick,
                 std::function<void(int64_t id, float x, float y)> onRightClick);

  // Wheel forwarder — iPlug2 dispatches OnMouseWheel to the topmost
  // control under the cursor; without this hook a wheel gesture over
  // a card never reaches LibraryView's scroll handler.
  void setOnWheel(std::function<void(float delta)> cb) { mOnWheel = std::move(cb); }

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const IMouseMod& mod) override;
  void OnMouseOut() override;
  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override;
  void OnResize() override;

  void setSelected(bool s);
  bool isSelected() const { return mSelected; }
  int64_t id() const { return mData.id; }

  // Mutate the data view-model and trigger a repaint. Used by
  // LibraryView when the same card slot is reused for a new row
  // (e.g. after refresh() following a rename).
  void setData(CardData data);

private:
  void RecomputeRects();

  CardData mData;
  std::function<void(int64_t)> mOnClick;
  std::function<void(int64_t, float, float)> mOnRightClick;
  std::function<void(float)> mOnWheel;

  bool  mSelected = false;
  bool  mHovered  = false;

  // Cached layout rects.
  IRECT mHeroRect;
  IRECT mNameRect;
  IRECT mMetaRect;
};

}  // namespace t3k::ui
