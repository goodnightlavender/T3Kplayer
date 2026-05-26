#include "T3kModelTile.h"

#include <cstdio>
#include "IGraphics.h"
#include "../theme.h"

namespace t3k::ui {

using namespace iplug::igraphics;

T3kModelTile::T3kModelTile(const IRECT& bounds,
                           int slotIndex,
                           Variant variant,
                           GearType iconType,
                           std::function<void(int)> onSelect,
                           std::function<void(int)> onAdd,
                           std::function<void(int)> onBypassToggle)
: IControl(bounds)
, mSlotIndex(slotIndex)
, mVariant(variant)
, mIconType(iconType)
, mOnSelect(std::move(onSelect))
, mOnAdd(std::move(onAdd))
, mOnBypassToggle(std::move(onBypassToggle))
{
}

void T3kModelTile::setVariant(Variant v)   { if (mVariant   != v) { mVariant   = v; mIconSvg.reset(); SetDirty(false); } }
void T3kModelTile::setIconType(GearType t) { if (mIconType  != t) { mIconType  = t; mIconSvg.reset(); SetDirty(false); } }
void T3kModelTile::setName(std::string n)  { if (mName      != n) { mName      = std::move(n); SetDirty(false); } }
void T3kModelTile::setSelected(bool s)     { if (mSelected  != s) { mSelected  = s; SetDirty(false); } }
void T3kModelTile::setBypassed(bool b)     { if (mBypassed  != b) { mBypassed  = b; SetDirty(false); } }
void T3kModelTile::setValues(Values v)     { mValues = v; SetDirty(false); }

void T3kModelTile::setDragBoundsX(float minOffsetX, float maxOffsetX)
{
  mDragMinOffset = minOffsetX;
  mDragMaxOffset = maxOffsetX;
}

void T3kModelTile::OnMouseDown(float, float, const IMouseMod&)
{
  if (mVariant == Variant::Empty)
  {
    if (mOnAdd) mOnAdd(mSlotIndex);
    return;
  }
  if (mOnSelect) mOnSelect(mSlotIndex);
}

void T3kModelTile::OnMouseDblClick(float, float, const IMouseMod&)
{
  if (mVariant == Variant::Empty) return;
  if (mOnBypassToggle) mOnBypassToggle(mSlotIndex);
}

void T3kModelTile::OnMouseDrag(float, float, float dX, float, const IMouseMod&)
{
  if (mVariant == Variant::Empty) return;
  if (!mDragging)
  {
    mDragging = true;
    if (mOnDragStart) mOnDragStart(mSlotIndex);
  }
  mDragOffsetX += dX;
  if (mDragOffsetX < mDragMinOffset) mDragOffsetX = mDragMinOffset;
  if (mDragOffsetX > mDragMaxOffset) mDragOffsetX = mDragMaxOffset;
  if (mOnDragMove) mOnDragMove(mSlotIndex, mRECT.L + mDragOffsetX, mRECT.MH());
  SetDirty(false);
}

void T3kModelTile::OnMouseUp(float, float, const IMouseMod&)
{
  if (!mDragging) return;
  if (mOnDragEnd) mOnDragEnd(mSlotIndex, mRECT.L + mDragOffsetX, mRECT.MH());
  mDragging    = false;
  mDragOffsetX = 0.f;
  SetDirty(false);
}

void T3kModelTile::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  if (mVariant == Variant::Empty)
  {
    g.DrawDottedRect(IColor(255, 34, 34, 34), mRECT, nullptr, 1.f, 4.f);
    const IText plus(18.f, IColor(255, 68, 68, 68), th::kFontBodyBold,
                     EAlign::Center, EVAlign::Middle);
    g.DrawText(plus, "+", mRECT);
    return;
  }

  IRECT body = mRECT;
  if (mDragging) body = body.GetTranslated(mDragOffsetX, 0.f);

  const IColor border = mSelected ? th::kAccent : IColor(255, 29, 29, 29);
  const IColor fill   = mSelected ? IColor(255, 20, 20, 0) : IColor(255, 13, 13, 13);
  g.FillRoundRect(fill, body, 5.f);
  g.DrawRoundRect(border, body, 5.f, nullptr, 1.f);

  if (mBypassed)
    g.FillRoundRect(IColor(128, 10, 10, 10), body, 5.f);

  const float iconSide = 22.f;
  const IRECT iconR(body.MW() - iconSide * 0.5f,
                    body.T + 6.f,
                    body.MW() + iconSide * 0.5f,
                    body.T + 6.f + iconSide);
  if (!mIconSvg.has_value())
    mIconSvg.emplace(g.LoadSVG(T3kGearIcon::filenameFor(mIconType)));
  if (mIconSvg.has_value())
    T3kGearIcon::drawInto(g, *mIconSvg, mIconType, iconR);

  const IRECT nameR(body.L + 2.f, iconR.B + 3.f, body.R - 2.f, iconR.B + 13.f);
  const IText nameT(9.f, th::kText, th::kFontBodyBold,
                    EAlign::Center, EVAlign::Middle);
  g.DrawText(nameT, mName.c_str(), nameR);

  const float numY = nameR.B + 2.f;
  const IText numT(8.f, th::kTextMuted, th::kFontBody,
                   EAlign::Center, EVAlign::Top);
  auto drawRow = [&](float y, int a, int b, int c) {
    const float colW = (body.W() - 6.f) / 3.f;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", a);
    g.DrawText(numT, buf, IRECT(body.L + 3.f, y, body.L + 3.f + colW, y + 10.f));
    std::snprintf(buf, sizeof(buf), "%d", b);
    g.DrawText(numT, buf, IRECT(body.L + 3.f + colW, y, body.L + 3.f + 2.f * colW, y + 10.f));
    std::snprintf(buf, sizeof(buf), "%d", c);
    g.DrawText(numT, buf, IRECT(body.L + 3.f + 2.f * colW, y, body.R - 3.f, y + 10.f));
  };
  drawRow(numY,        mValues.bass,  mValues.mid,    mValues.treble);
  drawRow(numY + 11.f, mValues.inDb,  mValues.outDb,  mValues.dryWet);
}

}  // namespace t3k::ui
