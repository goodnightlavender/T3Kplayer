#include "T3kModelTile.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "IGraphics.h"
#include "../theme.h"
#include "../text_util.h"

namespace t3k::ui {

using namespace iplug::igraphics;

T3kModelTile::T3kModelTile(const IRECT& bounds,
                           int slotIndex,
                           Variant variant,
                           GearType iconType,
                           std::function<void(int)> onSelect,
                           std::function<void(int)> onAdd,
                           std::function<void(int)> onBypassToggle,
                           std::function<void(int)> onDelete)
: IControl(bounds)
, mSlotIndex(slotIndex)
, mVariant(variant)
, mIconType(iconType)
, mOnSelect(std::move(onSelect))
, mOnAdd(std::move(onAdd))
, mOnBypassToggle(std::move(onBypassToggle))
, mOnDelete(std::move(onDelete))
{
}

void T3kModelTile::setVariant(Variant v)   { if (mVariant   != v) { mVariant   = v; mIconSvg.reset(); SetDirty(false); } }
void T3kModelTile::setIconType(GearType t) { if (mIconType  != t) { mIconType  = t; mIconSvg.reset(); SetDirty(false); } }
void T3kModelTile::setName(std::string n)  { if (mName      != n) { mName      = std::move(n); SetDirty(false); } }
void T3kModelTile::setSelected(bool s)     { if (mSelected  != s) { mSelected  = s; SetDirty(false); } }
void T3kModelTile::setBypassed(bool b)     { if (mBypassed  != b) { mBypassed  = b; SetDirty(false); } }
void T3kModelTile::setDisabled(bool d)     { if (mDisabled  != d) { mDisabled  = d; SetDirty(false); } }
void T3kModelTile::setValues(Values v)
{
  // Delta gate — ToneView::IsDirty pushes these every frame; only mark
  // dirty when something actually changed, so iPlug2's repaint loop
  // can stay quiescent at idle.
  if (mValues.bass   == v.bass   && mValues.mid    == v.mid    &&
      mValues.treble == v.treble && mValues.inDb   == v.inDb   &&
      mValues.outDb  == v.outDb  && mValues.dryWet == v.dryWet)
    return;
  mValues = v;
  SetDirty(false);
}

void T3kModelTile::setDragBoundsX(float minOffsetX, float maxOffsetX)
{
  mDragMinOffset = minOffsetX;
  mDragMaxOffset = maxOffsetX;
}

void T3kModelTile::setHomeRect(const IRECT& r)
{
  mHomeRect = r;
  mHasHomeRect = true;
}

IRECT T3kModelTile::deleteRect(const IRECT& body) const
{
  return IRECT(body.R - 18.f, body.T + 5.f, body.R - 5.f, body.T + 18.f);
}

void T3kModelTile::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  if (mVariant == Variant::Empty)
  {
    if (mDisabled) return;
    if (mOnAdd) mOnAdd(mSlotIndex);
    return;
  }
  const IRECT body = mHasHomeRect ? mHomeRect : mRECT;
  if (deleteRect(body).Contains(x, y)) {
    if (mOnDelete) mOnDelete(mSlotIndex);
    return;
  }
  if (mod.R) {
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
  if (!mOnDragMove && !mOnDragEnd) return;
  if (!mDragging)
  {
    if (!mHasHomeRect) setHomeRect(mRECT);
    mDragging = true;
    const IRECT dragClip(mHomeRect.L + mDragMinOffset, mHomeRect.T,
                         mHomeRect.R + mDragMaxOffset, mHomeRect.B);
    SetTargetAndDrawRECTs(dragClip);
    if (mOnDragStart) mOnDragStart(mSlotIndex);
  }
  mDragOffsetX += dX;
  if (mDragOffsetX < mDragMinOffset) mDragOffsetX = mDragMinOffset;
  if (mDragOffsetX > mDragMaxOffset) mDragOffsetX = mDragMaxOffset;
  if (mOnDragMove) mOnDragMove(mSlotIndex, mHomeRect.L + mDragOffsetX, mHomeRect.MH());
  SetDirty(false);
}

void T3kModelTile::OnMouseUp(float, float, const IMouseMod&)
{
  if (!mDragging) return;
  const float dropX = mHomeRect.L + mDragOffsetX;
  const float dropY = mHomeRect.MH();
  mDragging    = false;
  mDragOffsetX = 0.f;
  if (mHasHomeRect) SetTargetAndDrawRECTs(mHomeRect);
  if (mOnDragEnd) mOnDragEnd(mSlotIndex, dropX, dropY);
  SetDirty(false);
}

namespace {

// 2026-05-26 polish-pass — sizing constants scaled ~1.5× from the 720 px
// mockup so the tile reads at the plug-in's actual ~1024 px design canvas.
//
//   icon: 22 → 36
//   name: 9  → 13 (wrapped to 2 lines with ellipsis)
//   numeral row: 8 → 11
//
// Tile total content height = icon (36) + 4 + name (2 × 14 ≈ 28) + 4 +
// 2 numeral rows (12 each + 2 gap) ≈ 100. Add a few pixels of vertical
// padding and the parent strip needs ~110 px of slot space; ToneView
// bumps kStripH accordingly.
constexpr float kIconSide      = 44.f;
constexpr float kIconTopPad    = 10.f;
constexpr float kIconNameGap   = 3.f;
constexpr float kNameFontPx    = 13.f;
constexpr float kNameLineH     = 14.f;
constexpr float kNameMaxLines  = 2;
constexpr float kNameNumGap    = 4.f;
constexpr float kNumFontPx     = 11.f;
constexpr float kNumLineH      = 13.f;
constexpr float kNumRowGap     = 1.f;

// Word-wrap `s` into at most `maxLines` of width ~`maxCharsPerLine`.
// Mirrors T3kDetailModal::wrapText but stays inline because the tile
// doesn't already pull that helper. If the source overflows, the last
// emitted line gets an ASCII "..." ellipsis (text_util already runs
// before this so we don't have to re-sanitize).
std::vector<std::string> wrapNameTwoLines(const std::string& s,
                                          size_t maxCharsPerLine,
                                          size_t maxLines)
{
  std::vector<std::string> out;
  if (s.empty() || maxCharsPerLine == 0) {
    out.push_back(s);
    return out;
  }
  std::string line;
  size_t i = 0;
  while (i < s.size() && out.size() < maxLines) {
    const size_t sp = s.find(' ', i);
    const size_t wend = (sp == std::string::npos) ? s.size() : sp;
    std::string word = s.substr(i, wend - i);
    if (line.empty()) {
      // Word longer than a line on its own — hard-clip mid-word.
      if (word.size() > maxCharsPerLine) {
        out.push_back(word.substr(0, maxCharsPerLine));
        word.erase(0, maxCharsPerLine);
        // Push the remainder back into the loop as the next line's seed.
        line = std::move(word);
      } else {
        line = std::move(word);
      }
    } else if (line.size() + 1 + word.size() <= maxCharsPerLine) {
      line += " ";
      line += word;
    } else {
      out.push_back(std::move(line));
      line = std::move(word);
    }
    i = (sp == std::string::npos) ? s.size() : sp + 1;
  }
  if (!line.empty() && out.size() < maxLines) out.push_back(std::move(line));
  // If we ran out of lines but text remains, append ellipsis to the last.
  if (i < s.size() && !out.empty()) {
    std::string& last = out.back();
    // Trim until "..." fits.
    while (!last.empty() && last.size() + 3 > maxCharsPerLine) last.pop_back();
    last += "...";
  }
  return out;
}

}  // namespace

void T3kModelTile::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  if (mVariant == Variant::Empty)
  {
    const IColor dash = mDisabled ? IColor(255, 20, 20, 20)
                                  : IColor(255, 34, 34, 34);
    const IColor plusC = mDisabled ? IColor(255, 36, 36, 36)
                                   : IColor(255, 68, 68, 68);
    g.DrawDottedRect(dash, mRECT, nullptr, 1.f, 4.f);
    const IText plus(24.f, plusC, th::kFontBodyBold,
                     EAlign::Center, EVAlign::Middle);
    if (!mDisabled) {
      g.DrawText(plus, "+", mRECT);
    } else {
      const IColor xC(255, 70, 70, 70);
      const IRECT xR = mRECT.GetPadded(-22.f);
      g.DrawLine(xC, xR.L, xR.T, xR.R, xR.B, nullptr, 2.f);
      g.DrawLine(xC, xR.R, xR.T, xR.L, xR.B, nullptr, 2.f);
      const IText fullRigT(12.f, IColor(255, 105, 105, 105), th::kFontBodyBold,
                           EAlign::Center, EVAlign::Middle);
      g.DrawText(fullRigT, "FULL RIG", mRECT);
    }
    return;
  }

  IRECT body = mHasHomeRect ? mHomeRect : mRECT;
  if (mDragging) body = body.GetTranslated(mDragOffsetX, 0.f);

  const IColor border = mSelected ? th::kAccent : IColor(255, 29, 29, 29);
  const IColor fill   = mSelected ? IColor(255, 20, 20, 0) : IColor(255, 13, 13, 13);
  g.FillRoundRect(fill, body, 5.f);
  g.DrawRoundRect(border, body, 5.f, nullptr, 1.f);

  const IRECT del = deleteRect(body);
  g.FillCircle(IColor(255, 24, 24, 24), del.MW(), del.MH(), del.W() * 0.5f);
  g.DrawCircle(IColor(255, 90, 90, 90), del.MW(), del.MH(), del.W() * 0.5f,
               nullptr, 1.f);
  g.DrawLine(th::kTextMuted, del.L + 4.f, del.T + 4.f, del.R - 4.f, del.B - 4.f,
             nullptr, 1.2f);
  g.DrawLine(th::kTextMuted, del.R - 4.f, del.T + 4.f, del.L + 4.f, del.B - 4.f,
             nullptr, 1.2f);

  if (mBypassed)
    g.FillRoundRect(IColor(128, 10, 10, 10), body, 5.f);

  // Icon — centered horizontally and visually balanced above the name.
  float iconW = kIconSide;
  float iconH = kIconSide;
  if (mIconType == GearType::Amp) iconW = iconH = kIconSide * 1.10f;
  if (mIconType == GearType::Cab) iconW = iconH = kIconSide * 0.85f;
  if (mIconType == GearType::Outboard) iconW = iconH = kIconSide;
  iconW = std::min(iconW, body.W() - 12.f);
  iconH = std::min(iconH, kIconSide * 1.10f);
  float yNudge = 0.f;
  if (mIconType == GearType::Amp) yNudge = -1.f;
  if (mIconType == GearType::Cab) yNudge = 2.f;
  const IRECT iconR(body.MW() - iconW * 0.5f,
                    body.T + kIconTopPad + (kIconSide - iconH) * 0.5f + yNudge,
                    body.MW() + iconW * 0.5f,
                    body.T + kIconTopPad + (kIconSide - iconH) * 0.5f + yNudge + iconH);
  if (!mIconSvg.has_value())
    mIconSvg.emplace(g.LoadSVG(T3kGearIcon::filenameFor(mIconType)));
  if (mIconSvg.has_value())
    T3kGearIcon::drawInto(g, *mIconSvg, mIconType, iconR);

  // Pipe model name through the ASCII sanitizer so middle dots / em dashes
  // / ellipses from the catalog don't paint as tofu boxes.
  const std::string safeName = ::t3k::text_util::toAsciiSafe(mName);
  // Approximate char width for 13 px Inter Bold ~7 px/char. Tile widths in
  // the new scale sit around 110 px → ~14 chars per line — enough for most
  // model names; long ones wrap to a second line, anything still too long
  // gets "..." ellipsized.
  const float pxPerChar = 7.f;
  const size_t maxCharsPerLine = std::max<size_t>(
      4, static_cast<size_t>((body.W() - 4.f) / pxPerChar));
  std::vector<std::string> nameLines =
      wrapNameTwoLines(safeName, maxCharsPerLine,
                       static_cast<size_t>(kNameMaxLines));

  const IText nameT(kNameFontPx, th::kText, th::kFontBodyBold,
                    EAlign::Center, EVAlign::Middle);
  const float nameTop = iconR.B + kIconNameGap;
  for (size_t li = 0; li < nameLines.size(); ++li) {
    const IRECT nameR(body.L + 2.f,
                      nameTop + li * kNameLineH,
                      body.R - 2.f,
                      nameTop + (li + 1) * kNameLineH);
    g.DrawText(nameT, nameLines[li].c_str(), nameR);
  }
  const float nameBottom = nameTop + nameLines.size() * kNameLineH;

  const float numY = nameBottom + kNameNumGap;
  const IText numT(kNumFontPx, th::kTextMuted, th::kFontBody,
                   EAlign::Center, EVAlign::Top);
  auto drawRow = [&](float y, int a, int b, int c) {
    const float colW = (body.W() - 6.f) / 3.f;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", a);
    g.DrawText(numT, buf, IRECT(body.L + 3.f, y, body.L + 3.f + colW, y + kNumLineH));
    std::snprintf(buf, sizeof(buf), "%d", b);
    g.DrawText(numT, buf, IRECT(body.L + 3.f + colW, y, body.L + 3.f + 2.f * colW, y + kNumLineH));
    std::snprintf(buf, sizeof(buf), "%d", c);
    g.DrawText(numT, buf, IRECT(body.L + 3.f + 2.f * colW, y, body.R - 3.f, y + kNumLineH));
  };
  drawRow(numY,                      mValues.bass,  mValues.mid,    mValues.treble);
  drawRow(numY + kNumLineH + kNumRowGap, mValues.inDb,  mValues.outDb,  mValues.dryWet);
}

}  // namespace t3k::ui
