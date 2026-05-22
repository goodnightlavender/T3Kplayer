// T3kCard implementation. See T3kCard.h.

#include "T3kCard.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

constexpr float kImageSize       = 88.f;
constexpr float kAvatarSize      = 18.f;
constexpr float kDownloadPillW   = 36.f;
constexpr float kDownloadPillH   = 22.f;
constexpr float kCardHoverTime   = float(::t3k::theme::kAnimCardHover);

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

IColor LerpColor(const IColor& a, const IColor& b, float t)
{
  return IColor(
    int(Lerp(float(a.A), float(b.A), t)),
    int(Lerp(float(a.R), float(b.R), t)),
    int(Lerp(float(a.G), float(b.G), t)),
    int(Lerp(float(a.B), float(b.B), t)));
}

// Format an integer with thousands separators (e.g. 12345 -> "12,345").
// Locale-free; negative values handled.
std::string CommaFormat(int n)
{
  if (n == 0) return "0";

  const bool neg = (n < 0);
  long long v = neg ? -static_cast<long long>(n) : n;

  // Build raw digits (least-significant first), then re-emit with commas.
  std::string raw;
  while (v > 0) { raw.push_back(char('0' + (v % 10))); v /= 10; }

  std::string rev;
  for (size_t i = 0; i < raw.size(); ++i) {
    if (i > 0 && (i % 3 == 0)) rev.push_back(',');
    rev.push_back(raw[i]);
  }
  // `rev` is least-significant first — reverse for natural reading order.
  std::string out(rev.rbegin(), rev.rend());
  if (neg) out.insert(out.begin(), '-');
  return out;
}

}  // namespace

T3kCard::T3kCard(const IRECT& bounds,
                 CardData data,
                 std::function<void()> onSelect,
                 std::function<void()> onDownload)
: IControl(bounds)
, mData(std::move(data))
, mOnSelect(std::move(onSelect))
, mOnDownload(std::move(onDownload))
{
  RecomputeRects();
}

void T3kCard::OnResize()
{
  RecomputeRects();
}

void T3kCard::RecomputeRects()
{
  namespace th = ::t3k::theme;

  // Inner content rect (after card padding).
  const IRECT body = mRECT.GetPadded(-th::kS3);

  // Player strip sits at the bottom of mRECT (only drawn when mSelected).
  mPlayerStripRect = IRECT(mRECT.L, mRECT.B - kPlayerStripH,
                           mRECT.R, mRECT.B);

  // Top band — body, minus the player strip when expanded.
  const float bandBottom = mSelected ? (mPlayerStripRect.T - th::kS2)
                                     : body.B;
  const IRECT band(body.L, body.T, body.R, bandBottom);

  // Image square anchored to the top-left of the band.
  const float imgSz = std::min(kImageSize, band.H());
  mImageRect = IRECT(band.L, band.T,
                     band.L + imgSz, band.T + imgSz);

  // Right column starts kS3 to the right of the image.
  mRightColRect = IRECT(mImageRect.R + th::kS3, band.T,
                        band.R, band.B);

  // Download pill: top-right corner of the right column.
  mDownloadRect = IRECT(mRightColRect.R - kDownloadPillW,
                        mRightColRect.T,
                        mRightColRect.R,
                        mRightColRect.T + kDownloadPillH);
}

void T3kCard::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // ── Outer surface ──
  // Surface color lerps from kBgSurface -> kBgElevated as hover advances.
  float hoverT = mHoverTo;
  if (GetAnimationFunction()) {
    const float p = float(GetAnimationProgress());
    hoverT = Lerp(mHoverFrom, mHoverTo, p);
  }
  const IColor surface = LerpColor(th::kBgSurface, th::kBgElevated, hoverT);
  g.FillRoundRect(surface, mRECT, th::kRadiusLg);

  const IColor outline = mSelected ? th::kBorderActive : th::kBorder;
  g.DrawRoundRect(outline, mRECT, th::kRadiusLg,
                  /*pBlend*/ nullptr, /*thickness*/ 1.f);

  // ── Image square (or placeholder gradient) ──
  if (mData.image.has_value()) {
    g.DrawBitmap(*mData.image, mImageRect, /*srcX*/ 0, /*srcY*/ 0);
  } else {
    const IPattern grad = IPattern::CreateLinearGradient(
        mImageRect.L, mImageRect.T, mImageRect.L, mImageRect.B,
        {
          IColorStop(IColor(255, 26, 26, 26), 0.0f),
          IColorStop(IColor(255,  5,  5,  5), 1.0f),
        });
    g.PathClear();
    g.PathRoundRect(mImageRect, th::kRadiusMd);
    g.PathFill(grad);

    // Center a 4-char monogram from the title.
    const std::string mono = mData.title.substr(
        0, std::min<size_t>(4, mData.title.size()));
    const IText monoT(th::kTypeSmall,
                      th::kTextDim,
                      th::kFontBodySemi,
                      EAlign::Center,
                      EVAlign::Middle);
    g.DrawText(monoT, mono.c_str(), mImageRect);
  }

  // ── Right column rows ──
  const float rowH_title    = th::kTypeH2 + th::kS1;
  const float rowH_subtitle = th::kTypeSmall + th::kS1;
  const float rowH_stats    = th::kTypeSmall + th::kS1;
  const float rowH_creator  = std::max(kAvatarSize, th::kTypeSmall + th::kS1);

  float y = mRightColRect.T;

  // Title (truncated by the pill's left edge).
  const IRECT titleRect(mRightColRect.L, y,
                        mDownloadRect.L - th::kS2, y + rowH_title);
  const IText titleT(th::kTypeH2,
                     th::kText,
                     th::kFontBodyBold,
                     EAlign::Near,
                     EVAlign::Middle);
  g.DrawText(titleT, mData.title.c_str(), titleRect);
  y += rowH_title;

  // Download pill at the top-right of the right column.
  g.DrawRoundRect(th::kBorder, mDownloadRect, th::kRadiusPill,
                  /*pBlend*/ nullptr, /*thickness*/ 1.f);
  const IText pillT(th::kTypeSmall,
                    th::kTextMuted,
                    th::kFontBodySemi,
                    EAlign::Center,
                    EVAlign::Middle);
  g.DrawText(pillT, "GET", mDownloadRect);

  // Subtitle + optional inline badge.
  const IRECT subRect(mRightColRect.L, y,
                      mRightColRect.R, y + rowH_subtitle);
  const IText subT(th::kTypeSmall,
                   th::kTextMuted,
                   th::kFontBody,
                   EAlign::Near,
                   EVAlign::Middle);
  g.DrawText(subT, mData.subtitle.c_str(), subRect);

  if (!mData.badgeText.empty()) {
    IRECT subMeasured;
    g.MeasureText(subT, mData.subtitle.c_str(), subMeasured);
    const float badgeX = mRightColRect.L + subMeasured.W() + th::kS2;
    const float badgeW = 36.f;
    const IRECT badgeRect(badgeX, subRect.T + 1.f,
                          badgeX + badgeW, subRect.B - 1.f);
    g.DrawRoundRect(th::kBorder, badgeRect, th::kRadiusSm,
                    /*pBlend*/ nullptr, /*thickness*/ 1.f);
    const IText badgeT(th::kTypeLabel,
                       th::kTextMuted,
                       th::kFontBody,
                       EAlign::Center,
                       EVAlign::Middle);
    g.DrawText(badgeT, mData.badgeText.c_str(), badgeRect);
  }
  y += rowH_subtitle + th::kS2;

  // Stats row: downloads | bookmarks | modelCount, separated by kS5.
  const IRECT statsBand(mRightColRect.L, y,
                        mRightColRect.R, y + rowH_stats);
  const IText statT(th::kTypeSmall,
                    th::kTextMuted,
                    th::kFontBody,
                    EAlign::Near,
                    EVAlign::Middle);

  auto drawStat = [&](float& xCursor, const char* label) {
    IRECT m;
    g.MeasureText(statT, label, m);
    const IRECT cell(xCursor, statsBand.T,
                     xCursor + m.W() + 1.f, statsBand.B);
    g.DrawText(statT, label, cell);
    xCursor = cell.R + th::kS5;
  };

  float xCursor = statsBand.L;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "v %s",
                CommaFormat(mData.downloads).c_str());
  drawStat(xCursor, buf);
  std::snprintf(buf, sizeof(buf), "* %d", mData.bookmarks);
  drawStat(xCursor, buf);
  std::snprintf(buf, sizeof(buf), "# %d", mData.modelCount);
  drawStat(xCursor, buf);

  y += rowH_stats + th::kS2;

  // Creator row.
  const float avCY = y + rowH_creator * 0.5f;
  const float avCX = mRightColRect.L + kAvatarSize * 0.5f;
  const IPattern avGrad = IPattern::CreateLinearGradient(
      avCX - kAvatarSize * 0.5f, avCY - kAvatarSize * 0.5f,
      avCX + kAvatarSize * 0.5f, avCY + kAvatarSize * 0.5f,
      {
        IColorStop(IColor(255, 60, 60, 60), 0.0f),
        IColorStop(IColor(255, 20, 20, 20), 1.0f),
      });
  g.PathClear();
  g.PathCircle(avCX, avCY, kAvatarSize * 0.5f);
  g.PathFill(avGrad);

  std::string creatorLine = mData.creator;
  if (!mData.relativeDate.empty()) {
    creatorLine += " . ";
    creatorLine += mData.relativeDate;
  }
  const IRECT creatorRect(mRightColRect.L + kAvatarSize + th::kS2, y,
                          mRightColRect.R, y + rowH_creator);
  g.DrawText(statT, creatorLine.c_str(), creatorRect);

  // ── Player strip (only when selected) ──
  if (mSelected) {
    g.DrawLine(th::kBorder,
               mPlayerStripRect.L, mPlayerStripRect.T,
               mPlayerStripRect.R, mPlayerStripRect.T,
               /*pBlend*/ nullptr, 1.f);

    const float padX = th::kS3;
    const float padY = th::kS1;
    const IRECT stripInner = mPlayerStripRect.GetPadded(-padX, -padY,
                                                         -padX, -padY);

    // Play triangle at left.
    const float triCY = stripInner.MH();
    const float triCX = stripInner.L + 6.f;
    const float triH  = 10.f;
    g.PathClear();
    g.PathMoveTo(triCX - 5.f, triCY - triH * 0.5f);
    g.PathLineTo(triCX + 5.f, triCY);
    g.PathLineTo(triCX - 5.f, triCY + triH * 0.5f);
    g.PathClose();
    g.PathFill(th::kText);

    // "0:00" / "0:28" labels + rainbow track between them.
    const IText timeT(th::kTypeLabel,
                      th::kTextMuted,
                      th::kFontBody,
                      EAlign::Near,
                      EVAlign::Middle);
    const float timeStartX = triCX + 8.f + th::kS2;
    const IRECT startTimeRect(timeStartX, stripInner.T,
                              timeStartX + 30.f, stripInner.B);
    g.DrawText(timeT, "0:00", startTimeRect);

    const IText endTimeT(th::kTypeLabel,
                         th::kTextMuted,
                         th::kFontBody,
                         EAlign::Far,
                         EVAlign::Middle);
    const IRECT endTimeRect(stripInner.R - 30.f, stripInner.T,
                            stripInner.R, stripInner.B);
    g.DrawText(endTimeT, "0:28", endTimeRect);

    // FIXME(t3k): Phase 6 wires this to a real T3kRainbowScrubber + audio.
    const IRECT track(startTimeRect.R + th::kS2, stripInner.MH() - 1.5f,
                      endTimeRect.L - th::kS2, stripInner.MH() + 1.5f);
    const IPattern rainbow = IPattern::CreateLinearGradient(
        track.L, track.MH(), track.R, track.MH(),
        {
          IColorStop(th::kRainbowR, 0.0f),
          IColorStop(th::kRainbowY, 0.5f),
          IColorStop(th::kRainbowB, 1.0f),
        });
    g.PathClear();
    g.PathRect(track);
    g.PathFill(rainbow);
  }
}

void T3kCard::OnMouseDown(float x, float y, const IMouseMod& /*mod*/)
{
  // Download pill takes precedence over the body click.
  if (mDownloadRect.Contains(x, y)) {
    if (mOnDownload) mOnDownload();
    SetDirty(false);
    return;
  }

  // Clicks on the player strip (when present) are no-ops in Phase 2.
  if (mSelected && mPlayerStripRect.Contains(x, y)) return;

  if (mOnSelect) mOnSelect();
  SetDirty(false);
}

void T3kCard::OnMouseOver(float x, float y, const IMouseMod& mod)
{
  IControl::OnMouseOver(x, y, mod);
  if (mHovered) return;
  mHovered = true;
  mHoverFrom = mHoverTo;
  mHoverTo = 1.f;
  SetAnimation([](IControl* c) { c->SetDirty(false); },
               int(kCardHoverTime));
  SetDirty(false);
}

void T3kCard::OnMouseOut()
{
  IControl::OnMouseOut();
  if (!mHovered) return;
  mHovered = false;
  mHoverFrom = mHoverTo;
  mHoverTo = 0.f;
  SetAnimation([](IControl* c) { c->SetDirty(false); },
               int(kCardHoverTime));
  SetDirty(false);
}

}  // namespace t3k::ui
