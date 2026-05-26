// T3kLibraryCard.cpp — see T3kLibraryCard.h.

#include "T3kLibraryCard.h"

#include <algorithm>
#include <vector>

#include "IGraphics.h"

#include "../theme.h"
#include "../../cloud/ThumbnailCache.h"
#include "../../config.h"  // ICON_*_FN

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

constexpr float kCardPad     = 12.f;
constexpr float kHeroH       = 96.f;
constexpr float kNameH       = 34.f;
constexpr float kMetaH       = 14.f;

// Map gear_type -> resource filename. Falls back to the pedal icon.
const char* GearIconFor(const std::string& gearType)
{
  if (gearType == "amp")       return ICON_AMP_FN;
  if (gearType == "cab")       return ICON_CAB_FN;
  if (gearType == "outboard")  return ICON_OUTBOARD_FN;
  if (gearType == "full-rig")  return ICON_FULLRIG_FN;
  return ICON_PEDAL_FN;
}

}  // namespace

T3kLibraryCard::T3kLibraryCard(const IRECT& bounds,
                               CardData data,
                               std::function<void(int64_t)> onClick,
                               std::function<void(int64_t, float, float)> onRightClick)
: IControl(bounds)
, mData(std::move(data))
, mOnClick(std::move(onClick))
, mOnRightClick(std::move(onRightClick))
, mLogicalRect(bounds)
{
  RecomputeRects();
}

void T3kLibraryCard::setData(CardData data)
{
  const bool imageChanged = (data.imagePath != mData.imagePath) ||
                            (data.imageUrl  != mData.imageUrl);
  mData = std::move(data);
  if (imageChanged) {
    // Drop any cached bitmap so the next paint re-resolves the image
    // for the new card's path (or falls back to the gear icon).
    mBitmapLoaded     = false;
    mBitmapLoadFailed = false;
    mBitmap = IBitmap();
    mThumbRequested   = false;
    mThumbPath.clear();
    mThumbLoadFailed  = false;
  }
  SetDirty(false);
}

void T3kLibraryCard::setSelected(bool s)
{
  if (mSelected == s) return;
  mSelected = s;
  SetDirty(false);
}

void T3kLibraryCard::OnResize()
{
  // LibraryView supplies a clipped mRECT for scroll scissoring and a separate
  // logical rect for stable content layout.
}

void T3kLibraryCard::setLogicalRect(const IRECT& r)
{
  if (r.L == mLogicalRect.L && r.T == mLogicalRect.T
      && r.R == mLogicalRect.R && r.B == mLogicalRect.B) return;
  mLogicalRect = r;
  RecomputeRects();
  SetDirty(false);
}

void T3kLibraryCard::RecomputeRects()
{
  const IRECT r = mLogicalRect.GetPadded(-kCardPad);
  // Hero is a square pinned to the top of the inner content rect. Width
  // drives height so the hero stays a perfect square regardless of
  // surrounding panel height (which is taller now to make room for the
  // text rows below).
  const float heroSide = r.W();
  mHeroRect = IRECT(r.L, r.T, r.R, r.T + heroSide);
  mNameRect = IRECT(r.L, mHeroRect.B + 6.f,
                    r.R, mHeroRect.B + 6.f + kNameH);
  mMetaRect = IRECT(r.L, mNameRect.B + 2.f,
                    r.R, mNameRect.B + 2.f + kMetaH);
}

void T3kLibraryCard::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  if (mod.R) {
    if (mOnRightClick) mOnRightClick(mData.id, x, y);
    return;
  }
  if (mOnClick) mOnClick(mData.id);
}

void T3kLibraryCard::OnMouseDblClick(float /*x*/, float /*y*/,
                                     const IMouseMod& /*mod*/)
{
  if (mOnDblClick) mOnDblClick(mData.id);
}

void T3kLibraryCard::OnMouseDrag(float, float, float, float dY, const IMouseMod&)
{
  if (mOnDrag) mOnDrag(dY);
}

void T3kLibraryCard::OnMouseOver(float /*x*/, float /*y*/, const IMouseMod& /*mod*/)
{
  if (!mHovered) {
    mHovered = true;
    SetDirty(false);
  }
}

void T3kLibraryCard::OnMouseOut()
{
  if (mHovered) {
    mHovered = false;
    SetDirty(false);
  }
}

void T3kLibraryCard::OnMouseWheel(float /*x*/, float /*y*/,
                                  const IMouseMod& /*mod*/, float d)
{
  if (mOnWheel) mOnWheel(d);
}

void T3kLibraryCard::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Card surface — kBgSurface fill + border. Selected cards get the
  // accent ring; hovered ones get a slightly elevated fill (matches
  // T3kCard's hover cue).
  const IColor& fill = mHovered ? th::kBgElevated : th::kBgSurface;
  g.FillRoundRect(fill, mLogicalRect, th::kRadiusMd);
  const IColor& outline = mSelected ? th::kBorderActive : th::kBorder;
  g.DrawRoundRect(outline, mLogicalRect, th::kRadiusMd, nullptr, mSelected ? 2.f : 1.f);

  // Hero — try the cached image first, fall back to the gear icon SVG.
  g.FillRoundRect(th::kBgBase, mHeroRect, th::kRadiusSm);
  g.DrawRoundRect(th::kBorder, mHeroRect, th::kRadiusSm, nullptr, 1.f);

  // If we don't have a local path but DO have a remote URL, kick off
  // a ThumbnailCache fetch on first paint. The callback fills
  // mThumbPath asynchronously; subsequent paints pick it up.
  if (!mData.imageUrl.empty() && mData.imagePath.empty()
      && !mThumbRequested && !mThumbLoadFailed) {
    mThumbRequested = true;
    ::t3k::cloud::ThumbnailCache::instance().fetch(
        mData.imageUrl,
        [this](const std::string& path, bool ok) {
          // Worker-thread callback — only stash + mark dirty here.
          if (ok && !path.empty()) {
            mThumbPath = path;
          } else {
            mThumbLoadFailed = true;
          }
          this->SetDirty(false);
        });
  }
  // Pick the effective path: explicit local path wins, otherwise the
  // ThumbnailCache-resolved one.
  const std::string& effectivePath =
      !mData.imagePath.empty() ? mData.imagePath : mThumbPath;

  if (!mBitmapLoaded && !mBitmapLoadFailed && !effectivePath.empty()) {
    try {
      mBitmap = g.LoadBitmap(effectivePath.c_str(), 1, false);
      mBitmapLoaded = mBitmap.W() > 0;
    } catch (...) {
      mBitmapLoadFailed = true;
    }
    if (!mBitmapLoaded) mBitmapLoadFailed = true;
  }
  if (mBitmapLoaded) {
    // 2026-05-26 — Fit-CONTAIN the hero rect (preserve aspect, letterbox
    // overflow). We previously used cover (std::max) but iPlug2 doesn't
    // apply a per-control scissor on DrawFittedBitmap, so taller-than-
    // wide source bitmaps spilled vertically over the card chrome —
    // pictures looked bigger than others. std::min keeps the rendered
    // image fully inside mHeroRect with consistent visual scale.
    const float bw = static_cast<float>(mBitmap.W());
    const float bh = static_cast<float>(mBitmap.H());
    const float scale = std::min(mHeroRect.W() / bw, mHeroRect.H() / bh);
    const float dstW = bw * scale;
    const float dstH = bh * scale;
    const float dstL = mHeroRect.MW() - dstW * 0.5f;
    const float dstT = mHeroRect.MH() - dstH * 0.5f;
    g.DrawFittedBitmap(mBitmap,
                       IRECT(dstL, dstT, dstL + dstW, dstT + dstH));
  } else {
    if (ISVG svg = g.LoadSVG(GearIconFor(mData.gearType)); svg.IsValid()) {
      const float inset = 18.f;
      g.DrawSVG(svg, mHeroRect.GetPadded(-inset));
    }
  }

  // Display name — single line. iPlug2's NanoVG DrawText doesn't truncate
  // with ellipsis on overflow; it just keeps painting past the rect. So
  // we approximate Inter-Medium @ 13pt at ~7 px/char and trim by hand.
  auto truncate = [](const std::string& src, float widthPx, float pxPerChar) {
    const int maxChars = std::max(1, static_cast<int>(widthPx / pxPerChar));
    if (static_cast<int>(src.size()) <= maxChars) return src;
    if (maxChars <= 3) return std::string(static_cast<size_t>(maxChars), '.');
    return src.substr(0, static_cast<size_t>(maxChars - 3)) + "...";
  };
  auto wrapTwo = [&truncate](const std::string& src, float widthPx) {
    const int maxChars = std::max(4, static_cast<int>(widthPx / 7.f));
    std::vector<std::string> lines;
    std::string line;
    size_t i = 0;
    while (i < src.size() && lines.size() < 2) {
      const size_t sp = src.find(' ', i);
      const size_t end = (sp == std::string::npos) ? src.size() : sp;
      const std::string word = src.substr(i, end - i);
      if (line.empty()) {
        line = word;
      } else if (static_cast<int>(line.size() + 1 + word.size()) <= maxChars) {
        line += " ";
        line += word;
      } else {
        lines.push_back(line);
        line = word;
      }
      i = (sp == std::string::npos) ? src.size() : sp + 1;
    }
    if (!line.empty() && lines.size() < 2) lines.push_back(line);
    if (lines.empty()) lines.push_back("");
    if (i < src.size() && !lines.empty()) {
      lines.back() = truncate(lines.back(), widthPx, 7.f);
    }
    for (auto& ln : lines) ln = truncate(ln, widthPx, 7.f);
    return lines;
  };
  const auto nameLines = wrapTwo(mData.displayName, mNameRect.W());
  for (size_t i = 0; i < nameLines.size(); ++i) {
    const IRECT lineR(mNameRect.L, mNameRect.T + static_cast<float>(i) * 16.f,
                      mNameRect.R, mNameRect.T + static_cast<float>(i + 1) * 16.f);
    g.DrawText(IText(13.f, th::kText, th::kFontBodyMed,
                     EAlign::Near, EVAlign::Top),
               nameLines[i].c_str(), lineR);
  }

  // Meta: creator . format.
  std::string meta;
  if (!mData.creator.empty()) meta = mData.creator;
  if (!mData.format.empty()) {
    if (!meta.empty()) meta += " \xC2\xB7 ";
    meta += mData.format;
  }
  const std::string metaOut = truncate(meta, mMetaRect.W(), 6.f);
  g.DrawText(IText(th::kTypeSmall, th::kTextMuted, th::kFontBody,
                   EAlign::Near, EVAlign::Middle),
             metaOut.c_str(), mMetaRect);
}

}  // namespace t3k::ui
