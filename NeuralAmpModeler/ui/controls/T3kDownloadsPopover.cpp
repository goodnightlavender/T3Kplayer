// T3kDownloadsPopover implementation. See T3kDownloadsPopover.h.

#include "T3kDownloadsPopover.h"

#include <string>

#include "IGraphics.h"

#include "../theme.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

constexpr float kPanelPad   = 10.f;
constexpr float kHeaderH    = 22.f;
constexpr float kRowH       = 36.f;
constexpr float kRowGap     = 4.f;

// Panel chrome — same surface palette as T3kPresetOverlay.
const IColor kPanelBg    {255,  12,  12,  12};  // #0c0c0c
const IColor kPanelBorder{255,  31,  31,  31};  // #1f1f1f
const IColor kRowBg      {255,  21,  21,  21};  // #151515

std::string stageLabel(const T3kDownloadsPill::Row& r)
{
  using Stage = ::t3k::cloud::DownloadStatus::Stage;
  switch (r.stage) {
    case Stage::Queued:      return "Queued";
    case Stage::Listing:     return "Listing models\xE2\x80\xA6";
    case Stage::Downloading: {
      const int n = r.model_index + 1;
      const int t = (r.total_models > 0) ? r.total_models : 1;
      return "Downloading " + std::to_string(n) + " of " + std::to_string(t);
    }
    case Stage::Writing:     return "Writing\xE2\x80\xA6";
    case Stage::Done:        return "Done";
    case Stage::Failed:      return r.error_message.empty()
                                    ? std::string("Failed")
                                    : ("Failed: " + r.error_message);
  }
  return {};
}

IColor stageColor(::t3k::cloud::DownloadStatus::Stage st)
{
  namespace th = ::t3k::theme;
  using Stage = ::t3k::cloud::DownloadStatus::Stage;
  switch (st) {
    case Stage::Done:   return th::kAccent;
    case Stage::Failed: return th::kWarning;
    default:            return th::kTextMuted;
  }
}

}  // namespace

T3kDownloadsPopover::T3kDownloadsPopover(const IRECT& bounds, RowsProvider provider)
: IControl(bounds)
, mProvider(std::move(provider))
{
}

void T3kDownloadsPopover::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  g.FillRoundRect(kPanelBg, mRECT, th::kRadiusLg);
  g.DrawRoundRect(kPanelBorder, mRECT, th::kRadiusLg, nullptr, 1.f);

  const IRECT inner = mRECT.GetPadded(-kPanelPad);

  // Header.
  const IRECT header(inner.L, inner.T, inner.R, inner.T + kHeaderH);
  g.DrawText(IText(th::kTypeSmall, th::kText, th::kFontBodyBold,
                   EAlign::Near, EVAlign::Middle),
             "Downloads", header);

  const std::vector<T3kDownloadsPill::Row> rows =
      mProvider ? mProvider() : std::vector<T3kDownloadsPill::Row>{};

  if (rows.empty()) {
    const IRECT empty(inner.L, header.B + kRowGap, inner.R, inner.B);
    g.DrawText(IText(th::kTypeSmall, th::kTextMuted, th::kFontBody,
                     EAlign::Center, EVAlign::Middle),
               "No active downloads", empty);
    return;
  }

  float y = header.B + kRowGap;
  for (const auto& r : rows) {
    if (y + kRowH > inner.B) break;  // simple clip — popover is fixed-size
    const IRECT row(inner.L, y, inner.R, y + kRowH);
    g.FillRoundRect(kRowBg, row, th::kRadiusSm);

    const IRECT title(row.L + 10.f, row.T + 4.f, row.R - 10.f, row.T + 20.f);
    g.DrawText(IText(th::kTypeSmall, th::kText, th::kFontBodySemi,
                     EAlign::Near, EVAlign::Middle),
               r.tone_title.c_str(), title);

    const std::string lbl = stageLabel(r);
    const IRECT stage(row.L + 10.f, row.T + 18.f, row.R - 10.f, row.B - 4.f);
    g.DrawText(IText(th::kTypeSmall, stageColor(r.stage), th::kFontBody,
                     EAlign::Near, EVAlign::Middle),
               lbl.c_str(), stage);

    y += kRowH + kRowGap;
  }
}

}  // namespace t3k::ui
