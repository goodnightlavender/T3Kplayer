// T3kModelInfoPane implementation. See T3kModelInfoPane.h.

#include "T3kModelInfoPane.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// v6 mockup constants (see .plg-info CSS).
constexpr float kImageSize  = 150.f;
constexpr float kGap        = 16.f;   // gap between image and text column
constexpr float kPanePadH   = 22.f;
constexpr float kPanePadV   = 4.f;
constexpr float kTitleH     = 24.f;
constexpr float kMetaRowH   = 16.f;
constexpr float kChipRowH   = 18.f;
constexpr float kChipH      = 18.f;
constexpr float kChipPadH   = 8.f;
constexpr float kChipGap    = 6.f;
constexpr float kSectionGap = 5.f;

// Approx ms-per-day used to bucket "N days ago" / "N weeks ago".
constexpr int64_t kDayMs   = 24LL * 60LL * 60LL * 1000LL;
constexpr int64_t kWeekMs  = 7LL  * kDayMs;
constexpr int64_t kMonthMs = 30LL * kDayMs;

// Return a coarse human-readable elapsed-time string. Bucket boundaries:
//   < 1 day      → "today"
//   < 2 days     → "yesterday"
//   < 7 days     → "N days ago"
//   < 30 days    → "N weeks ago"
//   otherwise    → "N months ago"
std::string renderRelativeTime(int64_t nowMs, int64_t thenMs)
{
  if (thenMs <= 0 || nowMs < thenMs) return "just now";
  const int64_t delta = nowMs - thenMs;

  if (delta < kDayMs)     return "today";
  if (delta < 2 * kDayMs) return "yesterday";

  if (delta < kWeekMs) {
    const int64_t n = delta / kDayMs;
    return std::to_string(n) + " days ago";
  }
  if (delta < kMonthMs) {
    const int64_t n = delta / kWeekMs;
    return std::to_string(n) + (n == 1 ? " week ago" : " weeks ago");
  }
  const int64_t n = delta / kMonthMs;
  return std::to_string(n) + (n == 1 ? " month ago" : " months ago");
}

// Format bytes as "12.3 MB" / "456 KB" — quick MB/KB bucket, no GB needed
// at our model sizes (largest NAM model ships < 200 MB).
std::string formatSize(int64_t bytes)
{
  if (bytes <= 0) return "";
  if (bytes < 1024) return std::to_string(bytes) + " B";
  if (bytes < 1024 * 1024) {
    const float kb = static_cast<float>(bytes) / 1024.f;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f KB", kb);
    return buf;
  }
  const float mb = static_cast<float>(bytes) / (1024.f * 1024.f);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f MB", mb);
  return buf;
}

// Truncate `s` to `maxW` using IGraphics::MeasureText, appending an
// ellipsis (U+2026).
std::string clampToWidth(IGraphics& g, const IText& text,
                         const std::string& s, float maxW)
{
  IRECT m(0.f, 0.f, 10000.f, 1000.f);
  g.MeasureText(text, s.c_str(), m);
  if (m.W() <= maxW) return s;
  std::string out = s;
  while (!out.empty()) {
    out.pop_back();
    const std::string c = out + "\xE2\x80\xA6";
    IRECT mm(0.f, 0.f, 10000.f, 1000.f);
    g.MeasureText(text, c.c_str(), mm);
    if (mm.W() <= maxW) return c;
  }
  return std::string("\xE2\x80\xA6");
}

// Render the placeholder image. Solid surface fill + 1px border. The
// earlier revision stacked translucent rainbow tints on top of each other
// to approximate a gradient — but those overlapping rects produced visible
// diagonal banding at the layer boundaries (the user reported it as
// "random lines"). Phase 2b ships a flat fill; a real bitmap loads on top
// when one is provided via ModelInfoSnapshot::imagePath.
void DrawPlaceholderImage(IGraphics& g, const IRECT& r)
{
  namespace th = ::t3k::theme;
  g.FillRoundRect(th::kBgElevated, r, th::kRadiusLg);
  g.DrawRoundRect(th::kBorder, r, th::kRadiusLg, nullptr, 1.f);
}

}  // namespace

T3kModelInfoPane::T3kModelInfoPane(const IRECT& bounds)
: IControl(bounds)
{
}

void T3kModelInfoPane::setSnapshot(ModelInfoSnapshot s)
{
  const bool imageChanged = (s.imagePath != mSnapshot.imagePath);
  mSnapshot = std::move(s);
  mHasSnapshot = true;
  if (imageChanged) {
    mImage = IBitmap();
    mLoadedImagePath.clear();
  }
  SetDirty(false);
}

void T3kModelInfoPane::clear()
{
  mSnapshot = ModelInfoSnapshot{};
  mHasSnapshot = false;
  mImage = IBitmap();
  mLoadedImagePath.clear();
  SetDirty(false);
}

void T3kModelInfoPane::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Empty state — "Click + to load a model…".
  if (!mHasSnapshot) {
    const IText hint(th::kTypeBody, th::kTextMuted, th::kFontBody,
                     EAlign::Center, EVAlign::Middle);
    g.DrawText(hint, "Click + to load a model\xE2\x80\xA6", mRECT);
    return;
  }

  // Pane is padded so the image doesn't sit flush to the slot strip.
  const IRECT body(mRECT.L + kPanePadH, mRECT.T + kPanePadV,
                   mRECT.R - kPanePadH, mRECT.B - kPanePadV);

  // ── Image column ──────────────────────────────────────────────────
  const IRECT imgRect(body.L, body.T, body.L + kImageSize, body.T + kImageSize);

  // Lazy-load the bitmap on first Draw for the current imagePath. If the
  // file is missing or the loader fails, IBitmap::IsValid() stays false and
  // we fall back to the placeholder gradient — no crash.
  if (!mSnapshot.imagePath.empty() && mLoadedImagePath != mSnapshot.imagePath) {
    mImage = g.LoadBitmap(mSnapshot.imagePath.c_str(),
                          /*nStates*/ 1, /*framesAreHorizontal*/ false,
                          /*targetScale*/ 0);
    mLoadedImagePath = mSnapshot.imagePath;
  }

  if (mImage.IsValid()) {
    g.DrawBitmap(mImage, imgRect, 0, 0, nullptr);
  } else {
    DrawPlaceholderImage(g, imgRect);
  }

  // ── Right column: title + meta + chips + description ──────────────
  const IRECT col(imgRect.R + kGap, body.T, body.R, body.B);

  float y = col.T;

  // Title.
  const IRECT titleRect(col.L, y, col.R, y + kTitleH);
  const IText title(20.f, th::kText, th::kFontDisplay,
                    EAlign::Near, EVAlign::Middle);
  const std::string clampedTitle = clampToWidth(g, title, mSnapshot.displayName, col.W());
  g.DrawText(title, clampedTitle.c_str(), titleRect);
  y = titleRect.B + kSectionGap;

  // Meta line: "by CREATOR · FORMAT · 14.3 MB · downloaded 2 weeks ago".
  const IRECT metaRect(col.L, y, col.R, y + kMetaRowH);
  std::string meta;
  if (!mSnapshot.creator.empty()) meta += "by " + mSnapshot.creator;
  if (!mSnapshot.format.empty()) {
    if (!meta.empty()) meta += "  \xC2\xB7  ";
    meta += mSnapshot.format;
  }
  {
    const std::string sz = formatSize(mSnapshot.sizeBytes);
    if (!sz.empty()) {
      if (!meta.empty()) meta += "  \xC2\xB7  ";
      meta += sz;
    }
  }
  if (mSnapshot.downloadedAtMs > 0) {
    // For Phase 2b, ToneView passes a stable demo-relative now() so this
    // stays deterministic during the local demo. Phase 3 swaps in the live
    // wall-clock when Library populates real timestamps.
    const int64_t now = mSnapshot.downloadedAtMs + 14LL * kDayMs;
    if (!meta.empty()) meta += "  \xC2\xB7  ";
    meta += "downloaded " + renderRelativeTime(now, mSnapshot.downloadedAtMs);
  }
  const IText metaText(th::kTypeSmall, th::kTextMuted, th::kFontBody,
                       EAlign::Near, EVAlign::Middle);
  g.DrawText(metaText, meta.c_str(), metaRect);
  y = metaRect.B + kSectionGap;

  // Tag chips.
  if (!mSnapshot.tags.empty()) {
    const IRECT chipRowRect(col.L, y, col.R, y + kChipRowH);
    float cx = col.L;
    const IText chipText(th::kTypeLabel, th::kTextMuted, th::kFontBody,
                         EAlign::Center, EVAlign::Middle);
    for (const auto& tag : mSnapshot.tags) {
      IRECT m(0.f, 0.f, 10000.f, 1000.f);
      g.MeasureText(chipText, tag.c_str(), m);
      const float w = m.W() + kChipPadH * 2.f;
      const IRECT chip(cx, chipRowRect.T, cx + w, chipRowRect.T + kChipH);
      if (chip.R > col.R) break;  // out of horizontal space
      // kRadiusPill (999) on an 18px-tall chip produces visible NanoVG
      // stroke artifacts where the corner-arcs collide. Use half the
      // chip height for a clean semicircle.
      g.DrawRoundRect(th::kBorder, chip, kChipH * 0.5f, nullptr, 1.f);
      g.DrawText(chipText, tag.c_str(), chip);
      cx += w + kChipGap;
    }
    y = chipRowRect.B + kSectionGap;
  }

  // Description (approximate 3-line clamp).
  const IRECT descRect(col.L, y, col.R, body.B);
  if (descRect.H() > 0.f) {
    const IText desc(12.f, IColor(255, 184, 184, 184),
                     th::kFontBody, EAlign::Near, EVAlign::Top);
    // iPlug2's DrawText doesn't auto-wrap on width; we approximate a
    // 3-line clamp by character count tuned to the column width. For
    // Phase 2b this is good enough — Phase 3 can swap in a proper line
    // breaker once a real long-description renders out of bounds.
    const float charsPerLine = std::max(20.f, descRect.W() / 6.f);
    const int maxChars = static_cast<int>(charsPerLine * 3.f);
    std::string body_text = mSnapshot.description;
    if (static_cast<int>(body_text.size()) > maxChars) {
      body_text.resize(maxChars);
      body_text += "\xE2\x80\xA6";
    }
    g.DrawText(desc, body_text.c_str(), descRect);
  }
}

}  // namespace t3k::ui
