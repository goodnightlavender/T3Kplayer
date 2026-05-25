// T3kDownloadsPill implementation. See T3kDownloadsPill.h.

#include "T3kDownloadsPill.h"

#include <algorithm>

#include "IGraphics.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// Pill internal layout — matches T3kPresetPill visually.
constexpr float kPadLeft     = 10.f;
constexpr float kPadRight    = 10.f;
constexpr float kGlyphSize   = 12.f;
constexpr float kGlyphToText = 6.f;

}  // namespace

T3kDownloadsPill::T3kDownloadsPill(const IRECT& bounds,
                                   std::function<void()> onToggleOverlay)
: IControl(bounds)
, mOnToggleOverlay(std::move(onToggleOverlay))
{
}

T3kDownloadsPill::~T3kDownloadsPill()
{
  if (mListenerId > 0) {
    ::t3k::cloud::Downloader::instance().unsubscribe(mListenerId);
    mListenerId = 0;
  }
}

void T3kDownloadsPill::OnAttached()
{
  // Subscribe to the Downloader. Callback fires on the HTTP worker
  // thread; stash the snapshot under mMtx and mark dirty. SetDirty
  // from a non-GUI thread is the same pattern CloudView uses for the
  // same listener (see CloudView::OnAttached).
  mListenerId = ::t3k::cloud::Downloader::instance().subscribe(
      [this](const ::t3k::cloud::DownloadStatus& s) {
        Row r;
        r.id            = s.id;
        r.tone_id       = s.tone_id;
        r.tone_title    = s.tone_title;
        r.stage         = s.stage;
        r.model_index   = s.model_index;
        r.total_models  = s.total_models;
        r.error_message = s.error_message;
        {
          std::lock_guard<std::mutex> lk(mMtx);
          mRows[s.tone_id] = std::move(r);
        }
        SetDirty(false);
      });
}

std::vector<T3kDownloadsPill::Row> T3kDownloadsPill::snapshotRows() const
{
  std::vector<Row> out;
  std::lock_guard<std::mutex> lk(mMtx);
  out.reserve(mRows.size());
  for (const auto& kv : mRows) out.push_back(kv.second);
  // Sort: active first (by id desc → most recent enqueue at top), then
  // done/failed by id desc.
  std::sort(out.begin(), out.end(), [](const Row& a, const Row& b) {
    using Stage = ::t3k::cloud::DownloadStatus::Stage;
    const bool aActive = (a.stage != Stage::Done && a.stage != Stage::Failed);
    const bool bActive = (b.stage != Stage::Done && b.stage != Stage::Failed);
    if (aActive != bActive) return aActive && !bActive;
    return a.id > b.id;
  });
  return out;
}

int T3kDownloadsPill::activeCount() const
{
  using Stage = ::t3k::cloud::DownloadStatus::Stage;
  std::lock_guard<std::mutex> lk(mMtx);
  int n = 0;
  for (const auto& kv : mRows) {
    const auto st = kv.second.stage;
    if (st != Stage::Done && st != Stage::Failed) ++n;
  }
  return n;
}

void T3kDownloadsPill::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  const IColor stroke = mMouseIsOver ? th::kBorderActive : th::kBorder;
  const float pr = th::pillRadius(mRECT.H());
  g.FillRoundRect(th::kBgSurface, mRECT, pr);
  g.DrawRoundRect(stroke, mRECT, pr, nullptr, 1.f);

  // Download glyph — downward-pointing arrow (down line + down triangle).
  // The Inter subset shipped with the plug-in doesn't include U+2B07, so
  // draw it primitively to stay font-agnostic (same idiom as the
  // chevron on T3kPresetPill).
  const float glyphCx = mRECT.L + kPadLeft + kGlyphSize * 0.5f;
  const float glyphCy = mRECT.MH();
  const int n = activeCount();
  const IColor glyphCol = (n > 0) ? th::kAccent : th::kTextMuted;
  // vertical stem
  g.DrawLine(glyphCol, glyphCx, glyphCy - 5.f, glyphCx, glyphCy + 2.f, nullptr, 1.5f);
  // arrowhead
  g.FillTriangle(glyphCol,
                 glyphCx - 4.f, glyphCy + 1.f,
                 glyphCx + 4.f, glyphCy + 1.f,
                 glyphCx,       glyphCy + 5.f,
                 nullptr);

  // Badge: integer count of in-flight items. When zero, render a
  // muted "0" so the pill still reads as a downloads surface (the
  // user can click it to see Done/Failed history).
  const std::string label = std::to_string(n);
  const IRECT textRect(glyphCx + kGlyphSize * 0.5f + kGlyphToText,
                       mRECT.T,
                       mRECT.R - kPadRight,
                       mRECT.B);
  const IColor textCol = (n > 0) ? th::kText : th::kTextMuted;
  g.DrawText(IText(th::kTypeSmall,
                   textCol,
                   th::kFontBodySemi,
                   EAlign::Near,
                   EVAlign::Middle),
             label.c_str(), textRect);
}

void T3kDownloadsPill::OnMouseDown(float /*x*/, float /*y*/, const IMouseMod& /*mod*/)
{
  if (mOnToggleOverlay) mOnToggleOverlay();
  SetDirty(false);
}

void T3kDownloadsPill::OnMouseOver(float x, float y, const IMouseMod& mod)
{
  IControl::OnMouseOver(x, y, mod);
  SetDirty(false);
}

void T3kDownloadsPill::OnMouseOut()
{
  IControl::OnMouseOut();
  SetDirty(false);
}

}  // namespace t3k::ui
