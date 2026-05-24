// T3kDetailModal.cpp — see T3kDetailModal.h.

#include "T3kDetailModal.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "IGraphics.h"

#include "../theme.h"
#include "../controls/T3kButton.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

constexpr float kCardW       = 980.f;
constexpr float kCardH       = 620.f;
constexpr float kCardPad     = 28.f;
constexpr float kImageW      = 320.f;
constexpr float kImageH      = 320.f;
constexpr float kImageGap    = 28.f;
constexpr float kCloseSz     = 28.f;
constexpr float kActionBtnW  = 160.f;
constexpr float kActionBtnH  = 36.f;
constexpr float kActionGap   = 10.f;

// Body text metrics — approximate px/char for Inter at 13px. Used by
// wrapText since iPlug2 doesn't surface NanoVG's measureText synchronously.
constexpr float kBodyPxPerChar = 6.5f;
constexpr float kBodyLineH     = 18.f;

// Backdrop alpha — matches other modals.
const IColor kBackdrop {178, 0, 0, 0};  // ~70% black

}  // namespace

T3kDetailModal::T3kDetailModal(const IRECT& bounds, OnClose onClose)
: IControl(bounds)
, mOnClose(std::move(onClose))
{
  recomputeLayout();
}

void T3kDetailModal::OnResize()
{
  recomputeLayout();
}

void T3kDetailModal::recomputeLayout()
{
  const float cx = mRECT.MW();
  const float cy = mRECT.MH();
  mCardRect = IRECT(cx - kCardW * 0.5f, cy - kCardH * 0.5f,
                    cx + kCardW * 0.5f, cy + kCardH * 0.5f);

  const float imgT = mCardRect.T + kCardPad;
  mImageRect = IRECT(mCardRect.L + kCardPad, imgT,
                     mCardRect.L + kCardPad + kImageW,
                     imgT + kImageH);
  mTextRect  = IRECT(mImageRect.R + kImageGap, imgT,
                     mCardRect.R - kCardPad,
                     mCardRect.B - kCardPad - kActionBtnH - kActionGap);

  // Close button — top-right of the card.
  mCloseBtnRect = IRECT(mCardRect.R - kCardPad - kCloseSz,
                        mCardRect.T + kCardPad - 6.f,
                        mCardRect.R - kCardPad,
                        mCardRect.T + kCardPad - 6.f + kCloseSz);

  if (mCloseBtn) mCloseBtn->SetTargetAndDrawRECTs(mCloseBtnRect);

  // Lay out action buttons right-aligned along the bottom of the card.
  if (!mActionBtns.empty()) {
    const float by = mCardRect.B - kCardPad - kActionBtnH;
    float bx = mCardRect.R - kCardPad;
    // Walk right-to-left: the LAST element in mActionBtns lands closest
    // to the right edge.
    for (auto it = mActionBtns.rbegin(); it != mActionBtns.rend(); ++it) {
      T3kButton* btn = *it;
      if (!btn) continue;
      const IRECT r(bx - kActionBtnW, by, bx, by + kActionBtnH);
      btn->SetTargetAndDrawRECTs(r);
      bx -= kActionBtnW + kActionGap;
    }
  }
}

void T3kDetailModal::OnAttached()
{
  IGraphics* g = GetUI();
  if (!g) return;
  // Close (X) button — a tiny secondary T3kButton with the U+00D7
  // multiplication-sign glyph as the label.
  mCloseBtn = new T3kButton(mCloseBtnRect, "\xC3\x97",
      [this]() { if (mOnClose) mOnClose(); },
      T3kButton::Variant::Secondary);
  g->AttachControl(mCloseBtn);

  // Start hidden — show() unhides explicitly.
  const bool startHidden = IsHidden();
  mCloseBtn->Hide(startHidden);
}

void T3kDetailModal::show(DetailData data, std::vector<Action> actions)
{
  // Detach previous action buttons.
  IGraphics* g = GetUI();
  if (g) {
    for (T3kButton* b : mActionBtns) {
      if (b) g->RemoveControl(b);
    }
  }
  mActionBtns.clear();

  mData = std::move(data);
  mActions = std::move(actions);

  // Reset bitmap cache.
  mBitmapLoaded     = false;
  mBitmapLoadFailed = false;
  mBitmap = IBitmap();

  rebuildActionButtons();
  recomputeLayout();
  Hide(false);
  SetDirty(false);
}

void T3kDetailModal::rebuildActionButtons()
{
  IGraphics* g = GetUI();
  if (!g) return;
  // Build the buttons with placeholder rects; recomputeLayout fixes
  // them up immediately after.
  const IRECT placeholder(0.f, 0.f, 1.f, 1.f);
  for (const auto& a : mActions) {
    auto* btn = new T3kButton(placeholder, a.label.c_str(),
        a.onClick,
        a.primary ? T3kButton::Variant::Primary
                  : T3kButton::Variant::Secondary);
    g->AttachControl(btn);
    btn->Hide(IsHidden());
    mActionBtns.push_back(btn);
  }
}

void T3kDetailModal::OnMouseDown(float x, float y, const IMouseMod& /*mod*/)
{
  if (!mCardRect.Contains(x, y)) {
    if (mOnClose) mOnClose();
  }
}

void T3kDetailModal::Hide(bool hide)
{
  IControl::Hide(hide);
  if (mCloseBtn) mCloseBtn->Hide(hide);
  for (T3kButton* b : mActionBtns) if (b) b->Hide(hide);
}

std::vector<std::string>
T3kDetailModal::wrapText(const std::string& text,
                         float widthPx, float pxPerChar) const
{
  std::vector<std::string> lines;
  if (text.empty()) return lines;
  const int maxCharsPerLine =
      std::max(8, static_cast<int>(widthPx / pxPerChar));

  size_t paraStart = 0;
  while (paraStart <= text.size()) {
    const size_t nl = text.find('\n', paraStart);
    const size_t end = (nl == std::string::npos) ? text.size() : nl;
    const std::string para = text.substr(paraStart, end - paraStart);

    std::string line;
    size_t i = 0;
    while (i < para.size()) {
      const size_t sp = para.find(' ', i);
      const size_t wend = (sp == std::string::npos) ? para.size() : sp;
      const std::string word = para.substr(i, wend - i);
      if (line.empty()) {
        line = word;
      } else if (line.size() + 1 + word.size()
                   <= static_cast<size_t>(maxCharsPerLine)) {
        line += " ";
        line += word;
      } else {
        lines.push_back(std::move(line));
        line = word;
      }
      i = (sp == std::string::npos) ? para.size() : sp + 1;
    }
    if (!line.empty()) lines.push_back(std::move(line));
    if (nl == std::string::npos) break;
    if (paraStart != nl) lines.push_back("");
    paraStart = nl + 1;
  }
  return lines;
}

void T3kDetailModal::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Full-window dim layer.
  g.FillRect(kBackdrop, mRECT);

  // Card surface.
  g.FillRoundRect(th::kBgBase, mCardRect, th::kRadiusLg);
  g.DrawRoundRect(th::kBorder, mCardRect, th::kRadiusLg, nullptr, 1.f);

  // Image well.
  g.FillRoundRect(th::kBgSurface, mImageRect, th::kRadiusMd);
  g.DrawRoundRect(th::kBorder,    mImageRect, th::kRadiusMd, nullptr, 1.f);
  if (!mBitmapLoaded && !mBitmapLoadFailed && !mData.imagePath.empty()) {
    try {
      mBitmap = g.LoadBitmap(mData.imagePath.c_str(), 1, false);
      mBitmapLoaded = mBitmap.W() > 0;
    } catch (...) {
      mBitmapLoadFailed = true;
    }
    if (!mBitmapLoaded) mBitmapLoadFailed = true;
  }
  if (mBitmapLoaded) {
    const float bw = static_cast<float>(mBitmap.W());
    const float bh = static_cast<float>(mBitmap.H());
    const float scale = std::max(mImageRect.W() / bw, mImageRect.H() / bh);
    const float dstW = bw * scale;
    const float dstH = bh * scale;
    const float dstL = mImageRect.MW() - dstW * 0.5f;
    const float dstT = mImageRect.MH() - dstH * 0.5f;
    g.DrawFittedBitmap(mBitmap,
                       IRECT(dstL, dstT, dstL + dstW, dstT + dstH));
  } else {
    g.DrawText(IText(13.f, th::kTextDim, th::kFontBody,
                     EAlign::Center, EVAlign::Middle),
               "no image", mImageRect);
  }

  // ── Right column ──────────────────────────────────────────────
  const float tL = mTextRect.L;
  const float tR = mTextRect.R;
  float ty = mTextRect.T;

  // Title.
  const IRECT titleR(tL, ty, tR, ty + 40.f);
  g.DrawText(IText(28.f, th::kText, th::kFontBodyBold,
                   EAlign::Near, EVAlign::Top),
             mData.title.empty() ? "(no title)" : mData.title.c_str(),
             titleR);
  ty = titleR.B + 4.f;

  // Subtitle (accent).
  if (!mData.subtitle.empty()) {
    const IRECT subR(tL, ty, tR, ty + 22.f);
    g.DrawText(IText(14.f, th::kAccent, th::kFontBodyMed,
                     EAlign::Near, EVAlign::Top),
               mData.subtitle.c_str(), subR);
    ty = subR.B + 10.f;
  } else {
    ty += 4.f;
  }

  // Creator row — small avatar circle + name.
  if (!mData.creator.empty()) {
    const float avatarSz = 28.f;
    const IRECT avatarR(tL, ty, tL + avatarSz, ty + avatarSz);
    g.FillCircle(th::kBgSurface, avatarR.MW(), avatarR.MH(), avatarSz * 0.5f);
    g.DrawCircle(th::kBorder, avatarR.MW(), avatarR.MH(), avatarSz * 0.5f,
                 nullptr, 1.f);
    char initial[2] = { '?', '\0' };
    initial[0] = static_cast<char>(std::toupper(
        static_cast<unsigned char>(mData.creator[0])));
    g.DrawText(IText(13.f, th::kText, th::kFontBodyBold,
                     EAlign::Center, EVAlign::Middle),
               initial, avatarR);
    const IRECT creatorR(avatarR.R + 8.f, ty, tR, ty + avatarSz);
    g.DrawText(IText(14.f, th::kText, th::kFontBodyBold,
                     EAlign::Near, EVAlign::Middle),
               mData.creator.c_str(), creatorR);
    ty = avatarR.B + 14.f;
  }

  // 1px divider.
  g.FillRect(th::kBorder, IRECT(tL, ty, tR, ty + 1.f));
  ty += 14.f;

  // Description.
  if (!mData.description.empty()) {
    const IRECT hdrR(tL, ty, tR, ty + 18.f);
    g.DrawText(IText(14.f, th::kText, th::kFontBodyBold,
                     EAlign::Near, EVAlign::Top),
               "Description", hdrR);
    ty = hdrR.B + 6.f;
    const float wrapW = tR - tL;
    auto lines = wrapText(mData.description, wrapW, kBodyPxPerChar);
    const size_t maxLines = 4;
    if (lines.size() > maxLines) {
      lines.resize(maxLines);
      if (!lines.back().empty()) lines.back() += "\xE2\x80\xA6";
    }
    for (const auto& ln : lines) {
      const IRECT lineR(tL, ty, tR, ty + kBodyLineH);
      g.DrawText(IText(13.f, th::kTextMuted, th::kFontBody,
                       EAlign::Near, EVAlign::Top),
                 ln.c_str(), lineR);
      ty += kBodyLineH;
    }
    ty += 10.f;
  }

  // Makes and Models.
  if (!mData.makesModels.empty()) {
    const IRECT hdrR(tL, ty, tR, ty + 18.f);
    g.DrawText(IText(14.f, th::kText, th::kFontBodyBold,
                     EAlign::Near, EVAlign::Top),
               "Makes and Models", hdrR);
    ty = hdrR.B + 6.f;
    const size_t maxItems = 4;
    const size_t shown = std::min(mData.makesModels.size(), maxItems);
    for (size_t i = 0; i < shown; ++i) {
      const IRECT lineR(tL, ty, tR, ty + kBodyLineH);
      g.DrawText(IText(13.f, th::kAccent, th::kFontBody,
                       EAlign::Near, EVAlign::Top),
                 mData.makesModels[i].c_str(), lineR);
      ty += kBodyLineH;
    }
    if (mData.makesModels.size() > maxItems) {
      char hint[40];
      std::snprintf(hint, sizeof(hint),
                    "+%zu more\xE2\x80\xA6",
                    mData.makesModels.size() - maxItems);
      const IRECT lineR(tL, ty, tR, ty + kBodyLineH);
      g.DrawText(IText(13.f, th::kTextDim, th::kFontBody,
                       EAlign::Near, EVAlign::Top),
                 hint, lineR);
      ty += kBodyLineH;
    }
    ty += 10.f;
  }

  // Tags — pill chips, wrap when row overflows.
  if (!mData.tags.empty()) {
    const IRECT hdrR(tL, ty, tR, ty + 18.f);
    g.DrawText(IText(14.f, th::kText, th::kFontBodyBold,
                     EAlign::Near, EVAlign::Top),
               "Tags", hdrR);
    ty = hdrR.B + 6.f;
    const float chipH = 22.f;
    const float chipGap = 6.f;
    float x = tL;
    const size_t maxChips = 8;
    const size_t shown = std::min(mData.tags.size(), maxChips);
    for (size_t i = 0; i < shown; ++i) {
      const std::string& tag = mData.tags[i];
      const float chipW = std::max<float>(48.f,
                                          tag.size() * kBodyPxPerChar + 18.f);
      if (x + chipW > tR) {
        x = tL;
        ty += chipH + chipGap;
      }
      const IRECT chipR(x, ty, x + chipW, ty + chipH);
      g.FillRoundRect(th::kBgElevated, chipR, th::pillRadius(chipH));
      g.DrawRoundRect(th::kBorder,    chipR, th::pillRadius(chipH),
                      nullptr, 1.f);
      g.DrawText(IText(12.f, th::kText, th::kFontBody,
                       EAlign::Center, EVAlign::Middle),
                 tag.c_str(), chipR);
      x += chipW + chipGap;
    }
  }
}

}  // namespace t3k::ui
