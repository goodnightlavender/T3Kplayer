// T3kCard implementation. See T3kCard.h.

#include "T3kCard.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "IGraphics.h"

#include "../../cloud/ThumbnailCache.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

constexpr float kImageSize       = 88.f;
constexpr float kAvatarSize      = 18.f;
// DOWNLOAD pill — wider than the old "GET" so the full word fits at
// 11pt Inter SemiBold without truncation.
constexpr float kDownloadPillW   = 104.f;
constexpr float kDownloadPillH   = 22.f;
constexpr float kCardHoverTime   = float(::t3k::theme::kAnimCardHover);

// Stats-row glyph metrics. Each icon (download arrow / star / folder)
// occupies a 12-px square drawn immediately to the left of its number.
constexpr float kStatIconSz   = 12.f;
constexpr float kStatIconGap  = 4.f;

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

IColor LerpColor(const IColor& a, const IColor& b, float t)
{
  return IColor(
    int(Lerp(float(a.A), float(b.A), t)),
    int(Lerp(float(a.R), float(b.R), t)),
    int(Lerp(float(a.G), float(b.G), t)),
    int(Lerp(float(a.B), float(b.B), t)));
}

// ── Stats-row icon glyphs (download / favorites / model-count) ────
// Drawn as 12-px square paths instead of relying on font glyphs. The
// vendored Inter subset doesn't include U+2193 / U+2605 / folder
// emoji, which is why the previous "v" / "*" / "#" prefixes looked
// like leftover ASCII.

void DrawDownloadIcon(IGraphics& g, float cx, float cy, float sz, const IColor& col)
{
  // Vertical shaft + chevron point + base tray, ~⬇ shape.
  const float h = sz * 0.5f;
  const float w = sz * 0.5f;
  // Shaft.
  g.DrawLine(col, cx, cy - h * 0.9f, cx, cy + h * 0.2f, nullptr, 1.5f);
  // Chevron (V pointing down).
  g.DrawLine(col, cx - w * 0.55f, cy - h * 0.1f,
                  cx,               cy + h * 0.4f, nullptr, 1.5f);
  g.DrawLine(col, cx + w * 0.55f, cy - h * 0.1f,
                  cx,               cy + h * 0.4f, nullptr, 1.5f);
  // Tray.
  g.DrawLine(col, cx - w * 0.75f, cy + h * 0.85f,
                  cx + w * 0.75f, cy + h * 0.85f, nullptr, 1.5f);
}

void DrawStarIcon(IGraphics& g, float cx, float cy, float sz, const IColor& col)
{
  // 5-point star — outer radius = sz/2, inner radius = sz/2 * 0.45.
  // Build as a 10-vertex polygon and fill via PathConvexPolygon.
  const float ro = sz * 0.5f;
  const float ri = ro * 0.45f;
  float xs[10], ys[10];
  const float k = 3.14159265f / 180.f;
  for (int i = 0; i < 10; ++i) {
    const float a = (-90.f + i * 36.f) * k;
    const float r = (i % 2 == 0) ? ro : ri;
    xs[i] = cx + r * std::cos(a);
    ys[i] = cy + r * std::sin(a);
  }
  g.PathClear();
  g.PathMoveTo(xs[0], ys[0]);
  for (int i = 1; i < 10; ++i) g.PathLineTo(xs[i], ys[i]);
  g.PathClose();
  g.PathFill(col);
}

void DrawFolderIcon(IGraphics& g, float cx, float cy, float sz, const IColor& col)
{
  // Folder shape — small tab on top-left + main body.
  const float w = sz;
  const float h = sz * 0.85f;
  const float bx = cx - w * 0.5f;
  const float by = cy - h * 0.5f;
  const float tabW = w * 0.45f;
  const float tabH = h * 0.25f;
  // Tab.
  g.DrawRect(col, IRECT(bx, by, bx + tabW, by + tabH));
  // Body.
  g.DrawRect(col, IRECT(bx, by + tabH * 0.7f, bx + w, by + h));
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
, mLogicalRect(bounds)
{
  RecomputeRects();
}

void T3kCard::OnResize()
{
  // Intentionally a no-op. CloudView calls setLogicalRect to update
  // content layout; SetTargetAndDrawRECTs from layoutCards passes the
  // CLAMPED rect for scissor clipping, and we DO NOT want
  // RecomputeRects to run with that clamped rect (it would distort
  // content as the card scrolls into the body's top edge).
}

void T3kCard::setLogicalRect(const IRECT& r)
{
  // Skip if unchanged — RecomputeRects is cheap but SetDirty fires a
  // redraw and we call this on every scroll tick.
  if (r.L == mLogicalRect.L && r.T == mLogicalRect.T
      && r.R == mLogicalRect.R && r.B == mLogicalRect.B) return;
  mLogicalRect = r;
  RecomputeRects();
  SetDirty(false);
}

void T3kCard::RecomputeRects()
{
  namespace th = ::t3k::theme;

  // All content rects are derived from the LOGICAL rect — NOT mRECT.
  // mRECT may be clamped by CloudView's scroll clipping so iPlug2's
  // scissor stops the card from painting into the header. Content
  // positioned by the logical rect overflows that clamped area at
  // the top and is clipped automatically by the scissor.

  // Inner content rect (after card padding).
  const IRECT body = mLogicalRect.GetPadded(-th::kS3);

  // Player strip sits at the bottom of mLogicalRect (only drawn when
  // mSelected).
  mPlayerStripRect = IRECT(mLogicalRect.L, mLogicalRect.B - kPlayerStripH,
                           mLogicalRect.R, mLogicalRect.B);

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
  // Clamp progress to [0,1] — without the clamp, GetAnimationProgress
  // keeps growing past 1.0 (the animation lambda below doesn't end the
  // animation), and Lerp extrapolates the color past kBgElevated → the
  // tile slowly brightens toward white over many seconds. That was the
  // "15-second hover ramp" bug.
  float hoverT = mHoverTo;
  if (GetAnimationFunction()) {
    float p = float(GetAnimationProgress());
    if (p > 1.f) p = 1.f;
    if (p < 0.f) p = 0.f;
    hoverT = Lerp(mHoverFrom, mHoverTo, p);
  }
  const IColor surface = LerpColor(th::kBgSurface, th::kBgElevated, hoverT);
  // Background + border use mLogicalRect (the un-clamped position).
  // mRECT may be tighter (clamped by CloudView for scroll clipping)
  // and iPlug2's scissor — set from mRECT — clips the overflow.
  g.FillRoundRect(surface, mLogicalRect, th::kRadiusLg);

  const IColor outline = mSelected ? th::kBorderActive : th::kBorder;
  g.DrawRoundRect(outline, mLogicalRect, th::kRadiusLg,
                  /*pBlend*/ nullptr, /*thickness*/ 1.f);

  // ── Image square ──
  // Priority: explicit IBitmap > lazy-loaded thumb from URL > placeholder.
  //
  // First Draw with a non-empty imageUrl kicks off an async fetch via
  // ThumbnailCache. The callback writes the local path into mThumbPath
  // and marks dirty; subsequent Draws load the bitmap and cache it on
  // mThumbBitmap. If LoadBitmap returns an invalid bitmap (corrupt
  // file), mThumbLoadFailed flips so we don't spin retrying.
  if (!mData.image.has_value() && !mData.imageUrl.empty()
      && !mThumbRequested && !mThumbLoadFailed) {
    mThumbRequested = true;
    ::t3k::cloud::ThumbnailCache::instance().fetch(
        mData.imageUrl,
        [this](const std::string& path, bool ok) {
          // Worker-thread callback. Just stash the path + mark dirty;
          // GUI thread picks it up next paint.
          if (ok && !path.empty()) {
            mThumbPath = path;
          } else {
            mThumbLoadFailed = true;
          }
          this->SetDirty(false);
        });
  }
  if (!mData.image.has_value() && !mThumbBitmap.has_value()
      && !mThumbPath.empty() && !mThumbLoadFailed) {
    IBitmap bmp = g.LoadBitmap(mThumbPath.c_str(),
                                /*nStates*/ 1,
                                /*framesAreHorizontal*/ false,
                                /*targetScale*/ 0);
    if (bmp.W() > 0 && bmp.H() > 0) {
      mThumbBitmap = bmp;
    } else {
      mThumbLoadFailed = true;
    }
  }

  if (mData.image.has_value()) {
    g.DrawBitmap(*mData.image, mImageRect, /*srcX*/ 0, /*srcY*/ 0);
  } else if (mThumbBitmap.has_value()) {
    g.DrawFittedBitmap(*mThumbBitmap, mImageRect);
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

  // Download pill at the top-right of the right column. State drives
  // BOTH the fill color and the label:
  //   Idle    → border only, kTextMuted "DOWNLOAD"
  //   Active  → kAccent fill, white "DOWNLOADING…" (or "QUEUED" etc.)
  //   Done    → success-green fill, white "DOWNLOADED"
  //   Failed  → kError fill, white "FAILED"
  // The pill radius is clamped to half-height — kRadiusPill = 999 on a
  // 22-tall rect would otherwise blow PathRoundRect into screen-
  // spanning arcs (the diagonal-lines bug fixed in Phase 6 polish).
  const float pillR = th::pillRadius(mDownloadRect.H());
  IColor pillFill;
  IColor pillTextCol = th::kText;
  const char* defaultLabel = "DOWNLOAD";
  bool pillFilled = true;
  switch (mDlState) {
    case DownloadState::Idle:
      pillFilled = false;
      pillTextCol = th::kTextMuted;
      defaultLabel = "DOWNLOAD";
      break;
    case DownloadState::Active:
      pillFill = IColor(255, 255, 255, 255);
      pillTextCol = IColor(255, 0, 0, 0);
      defaultLabel = "DOWNLOADING\xE2\x80\xA6";
      break;
    case DownloadState::Done:
      // Success teal-green — kept inline because the theme palette
      // doesn't carry a dedicated kSuccess token at this revision.
      pillFill = IColor(255, 56, 176, 110);
      defaultLabel = "DOWNLOADED";
      break;
    case DownloadState::Failed:
      pillFill = th::kError;
      defaultLabel = "FAILED";
      break;
  }
  if (pillFilled) {
    g.FillRoundRect(pillFill, mDownloadRect, pillR);
  } else {
    g.DrawRoundRect(th::kBorder, mDownloadRect, pillR,
                    /*pBlend*/ nullptr, /*thickness*/ 1.f);
  }
  const IText pillT(th::kTypeSmall,
                    pillTextCol,
                    th::kFontBodySemi,
                    EAlign::Center,
                    EVAlign::Middle);
  const char* pillLabel =
      mDlLabel.empty() ? defaultLabel : mDlLabel.c_str();
  g.DrawText(pillT, pillLabel, mDownloadRect);

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

  // Each stat = icon glyph + number, separated by kS5. Drawn via
  // inline path so the icons render regardless of font subset (the
  // vendored Inter doesn't include arrow / star / folder glyphs).
  enum class Glyph { Download, Star, Folder };
  auto drawStat = [&](float& xCursor, Glyph glyph, const char* label) {
    const float iconCx = xCursor + kStatIconSz * 0.5f;
    const float iconCy = statsBand.MH();
    switch (glyph) {
      case Glyph::Download: DrawDownloadIcon(g, iconCx, iconCy, kStatIconSz, th::kTextMuted); break;
      case Glyph::Star:     DrawStarIcon(g, iconCx, iconCy, kStatIconSz, th::kTextMuted);     break;
      case Glyph::Folder:   DrawFolderIcon(g, iconCx, iconCy, kStatIconSz, th::kTextMuted);   break;
    }
    const float numX = xCursor + kStatIconSz + kStatIconGap;
    IRECT m;
    g.MeasureText(statT, label, m);
    const IRECT cell(numX, statsBand.T, numX + m.W() + 1.f, statsBand.B);
    g.DrawText(statT, label, cell);
    xCursor = cell.R + th::kS5;
  };

  float xCursor = statsBand.L;
  const std::string downloadsStr = CommaFormat(mData.downloads);
  const std::string bookmarksStr = std::to_string(mData.bookmarks);
  const std::string modelsStr    = std::to_string(mData.modelCount);
  drawStat(xCursor, Glyph::Download, downloadsStr.c_str());
  drawStat(xCursor, Glyph::Star,     bookmarksStr.c_str());
  drawStat(xCursor, Glyph::Folder,   modelsStr.c_str());

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

  // ── Player strip ──
  // Removed in polish round 3 — the in-card audio scrubber was never
  // wired to real playback, and the empty strip was visually noisy on
  // selected cards. Selection still shows via the accent border. The
  // future T3kRainbowScrubber design will land in a separate phase.
}

void T3kCard::OnMouseDown(float x, float y, const IMouseMod& /*mod*/)
{
  // Download pill takes precedence over the body click.
  if (mDownloadRect.Contains(x, y)) {
    if (mOnDownload) mOnDownload();
    SetDirty(false);
    return;
  }

  // (The old player-strip click-swallow lived here; removed in polish
  // round 3 along with the strip itself.)

  if (mOnSelect) mOnSelect();
  SetDirty(false);
}

void T3kCard::OnMouseDblClick(float x, float y, const IMouseMod& /*mod*/)
{
  // Don't trigger detail when the dbl-click landed on the Download
  // pill — that should remain a single-click action.
  if (mDownloadRect.Contains(x, y)) return;
  if (mOnDetail) mOnDetail();
}

void T3kCard::OnMouseDrag(float, float, float, float dY, const IMouseMod&)
{
  if (mOnDrag) mOnDrag(dY);
}

void T3kCard::OnMouseOver(float x, float y, const IMouseMod& mod)
{
  IControl::OnMouseOver(x, y, mod);
  if (mHovered) return;
  mHovered = true;
  mHoverFrom = mHoverTo;
  mHoverTo = 1.f;
  // Animation lambda repaints + ends the animation when progress > 1.0.
  // Without the OnEndAnimation call, mAnimationFunc stays non-null
  // forever and GetAnimationProgress keeps climbing, which is what
  // caused the original "hover slowly brightens to white over 15s" bug.
  SetAnimation([](IControl* c) {
                 if (c->GetAnimationProgress() > 1.) {
                   c->OnEndAnimation();
                   return;
                 }
                 c->SetDirty(false);
               },
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
  // Animation lambda repaints + ends the animation when progress > 1.0.
  // Without the OnEndAnimation call, mAnimationFunc stays non-null
  // forever and GetAnimationProgress keeps climbing, which is what
  // caused the original "hover slowly brightens to white over 15s" bug.
  SetAnimation([](IControl* c) {
                 if (c->GetAnimationProgress() > 1.) {
                   c->OnEndAnimation();
                   return;
                 }
                 c->SetDirty(false);
               },
               int(kCardHoverTime));
  SetDirty(false);
}

void T3kCard::OnMouseWheel(float /*x*/, float /*y*/,
                            const IMouseMod& /*mod*/, float d)
{
  // Forward to whatever scroll-handler the parent wired up. Without
  // this, wheel events over a card stop at the card (iPlug2 dispatches
  // to the topmost control under the cursor), and the cloud-results
  // list reads as unscrollable.
  if (mOnWheel) mOnWheel(d);
}

void T3kCard::setDownloadState(DownloadState s, std::string label)
{
  if (s == mDlState && label == mDlLabel) return;
  mDlState = s;
  mDlLabel = std::move(label);
  SetDirty(false);
}

void T3kCard::setData(CardData data)
{
  mData = std::move(data);
  // Drop the lazy-thumbnail cache. Without this, the previous tone's
  // bitmap (or its in-flight ThumbnailCache request) would still be
  // painted under the new title until the user scrolled or the card
  // got recreated. nullopt + cleared path + false flags forces the
  // next Draw to re-issue the lookup for the new tone's imageUrl.
  mThumbRequested  = false;
  mThumbPath.clear();
  mThumbBitmap.reset();
  mThumbLoadFailed = false;
  SetDirty(false);
}

}  // namespace t3k::ui
