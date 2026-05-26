// T3kDetailModal.cpp — see T3kDetailModal.h.

#include "T3kDetailModal.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "IGraphics.h"

#include "../theme.h"
#include "../controls/T3kButton.h"
#include "../../cloud/ThumbnailCache.h"

namespace t3k::ui {

using namespace ::iplug::igraphics;

namespace {

// 2026-05-25 — shrunk to fit the trimmed 1024x640 design canvas
// (was 980x620 on the previous 1100x688 canvas). Leaves ~52px
// horizontal margin and ~30px vertical margin inside the canvas.
constexpr float kCardW       = 920.f;
constexpr float kCardH       = 580.f;
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
  // Detach previous action + pick buttons.
  IGraphics* g = GetUI();
  if (g) {
    for (T3kButton* b : mActionBtns) if (b) g->RemoveControl(b);
    for (T3kButton* b : mPickBtns)   if (b) g->RemoveControl(b);
  }
  mActionBtns.clear();
  mPickBtns.clear();

  mData = std::move(data);
  mActions = std::move(actions);

  // Reset bitmap cache.
  mBitmapLoaded     = false;
  mBitmapLoadFailed = false;
  mBitmap = IBitmap();
  mThumbRequested   = false;
  mThumbPath.clear();
  mThumbLoadFailed  = false;
  // 2026-05-25 — every show() starts the Versions list scrolled to the
  // top so the user always sees variant 1 first.
  mPickablesScrollOffset = 0.f;

  rebuildActionButtons();
  // Build one PICK button per pickable variant. The actual rects are
  // computed during Draw (when we know the running y-cursor) — here
  // we just attach with a placeholder rect so iPlug has them in the
  // control list.
  if (g) {
    const IRECT placeholder(0.f, 0.f, 1.f, 1.f);
    for (const auto& p : mData.pickables) {
      // 2026-05-25 — relabelled from "PICK" to "LOAD" to match the
      // rest of the UI's language (the LOAD-INTO-CHAIN button on the
      // LibraryView's grid was removed in the same change; the detail
      // modal is now the single entry point for loading a library
      // model into the chain).
      // 2026-05-26 — Invert variant (white fill, black text) instead of
      // Primary. The Primary variant fills with kAccent (now #FFFF00 +
      // white label = unreadable). Invert keeps the loud-CTA energy on
      // a yellow background without the contrast collision.
      auto* btn = new T3kButton(placeholder, "LOAD",
          p.onPick, T3kButton::Variant::Invert);
      g->AttachControl(btn);
      btn->Hide(IsHidden());
      mPickBtns.push_back(btn);
    }
  }
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
    // 2026-05-26 — primary actions (e.g. Cloud DOWNLOAD) use the
    // Invert variant for the same reason as the LOAD buttons above:
    // kAccent is yellow now, and Primary's white label vanishes on it.
    auto* btn = new T3kButton(placeholder, a.label.c_str(),
        a.onClick,
        a.primary ? T3kButton::Variant::Invert
                  : T3kButton::Variant::Secondary);
    g->AttachControl(btn);
    btn->Hide(IsHidden());
    mActionBtns.push_back(btn);
  }
}

void T3kDetailModal::OnMouseDown(float x, float y, const IMouseMod& /*mod*/)
{
  // 2026-05-25 — scrollbar thumb drag. Check first so a thumb click
  // doesn't fall through to the dismiss check below. Track-click-
  // outside-thumb jump-scrolls the thumb's centre to that y position.
  if (mScrollbarThumbRect.W() > 0.f && mScrollbarThumbRect.H() > 0.f) {
    // Thumb hit: start drag, remembering where on the thumb the user
    // grabbed so the thumb doesn't jump under the cursor on first
    // OnMouseDrag.
    if (mScrollbarThumbRect.Contains(x, y)) {
      mScrollbarDragging  = true;
      mScrollbarDragGrabY = y - mScrollbarThumbRect.T;
      SetDirty(false);
      return;
    }
    // Track hit (same x range, inside the variants area vertically
    // but not on the thumb): jump-scroll so the thumb centres on y.
    if (x >= mScrollbarThumbRect.L && x <= mScrollbarThumbRect.R
        && y >= mPickablesAreaRect.T && y <= mPickablesAreaRect.B) {
      const float thumbH    = mScrollbarThumbRect.H();
      const float trackTop  = mPickablesAreaRect.T;
      const float trackH    = mPickablesAreaRect.H();
      const float maxOffset =
          std::max(1.f, mPickablesContentHeight - trackH);
      const float frac =
          std::clamp((y - trackTop - thumbH * 0.5f)
                       / std::max(1.f, trackH - thumbH),
                     0.f, 1.f);
      mPickablesScrollOffset = frac * maxOffset;
      mScrollbarDragging  = true;
      mScrollbarDragGrabY = thumbH * 0.5f;
      SetDirty(false);
      return;
    }
  }

  // 2026-05-26 — content drag. Mouse-down anywhere inside the
  // variants area that isn't a PICK button (children intercept their
  // own clicks before we see them) and isn't the scrollbar (checked
  // above) starts a drag-the-content scroll. The actual offset update
  // happens in OnMouseDrag from dY.
  if (mPickablesAreaRect.W() > 0.f && mPickablesAreaRect.Contains(x, y)
      && mPickablesContentHeight > mPickablesAreaRect.H() + 1.f) {
    mContentDragging = true;
    SetDirty(false);
    return;
  }

  if (!mCardRect.Contains(x, y)) {
    if (mOnClose) mOnClose();
  }
}

void T3kDetailModal::OnMouseDrag(float /*x*/, float y,
                                 float /*dX*/, float dY,
                                 const IMouseMod& /*mod*/)
{
  if (mScrollbarDragging) {
    // Convert the cursor's y position (minus the grab offset) into a
    // scroll offset by mapping along the track. The mapping mirrors
    // the Draw-time thumbY computation so a dragged thumb tracks the
    // cursor exactly.
    const float thumbH    = mScrollbarThumbRect.H();
    const float trackTop  = mPickablesAreaRect.T;
    const float trackH    = mPickablesAreaRect.H();
    if (thumbH <= 0.f || trackH <= 0.f) return;
    const float maxOffset =
        std::max(1.f, mPickablesContentHeight - trackH);
    const float frac =
        std::clamp((y - mScrollbarDragGrabY - trackTop)
                     / std::max(1.f, trackH - thumbH),
                   0.f, 1.f);
    mPickablesScrollOffset = frac * maxOffset;
    SetDirty(false);
    return;
  }

  if (mContentDragging) {
    // Touch-scroll mapping: dragging the cursor DOWN moves the
    // content down (offset decreases — like grabbing the page). dY
    // is delta-y from last position, positive = mouse moved down.
    mPickablesScrollOffset -= dY;
    const float maxOffset =
        std::max(0.f, mPickablesContentHeight - mPickablesAreaRect.H());
    if (mPickablesScrollOffset > maxOffset) mPickablesScrollOffset = maxOffset;
    if (mPickablesScrollOffset < 0.f)       mPickablesScrollOffset = 0.f;
    SetDirty(false);
    return;
  }
}

void T3kDetailModal::OnMouseUp(float /*x*/, float /*y*/,
                               const IMouseMod& /*mod*/)
{
  if (mScrollbarDragging || mContentDragging) {
    mScrollbarDragging = false;
    mContentDragging   = false;
    SetDirty(false);
  }
}

void T3kDetailModal::OnMouseWheel(float x, float y,
                                  const IMouseMod& /*mod*/, float d)
{
  // Only scroll when the cursor is over the Versions list. Outside
  // that region (over the image, the description, the action-button
  // row, or the backdrop) we ignore the wheel so the host can route
  // it to whatever's underneath. The area rect is recomputed each
  // Draw — if the modal hasn't drawn yet (zero area), do nothing.
  if (mPickablesAreaRect.W() <= 0.f || mPickablesAreaRect.H() <= 0.f) return;
  if (!mPickablesAreaRect.Contains(x, y)) return;

  // Wheel tick = a bit more than one row so a normal flick advances
  // by ~2 entries. d > 0 = wheel-up (scroll list up, smaller offset);
  // d < 0 = wheel-down (scroll list down, larger offset).
  constexpr float kStep = 45.f;
  mPickablesScrollOffset -= d * kStep;

  const float maxOffset =
      std::max(0.f, mPickablesContentHeight - mPickablesAreaRect.H());
  if (mPickablesScrollOffset > maxOffset) mPickablesScrollOffset = maxOffset;
  if (mPickablesScrollOffset < 0.f)       mPickablesScrollOffset = 0.f;

  SetDirty(false);
}

void T3kDetailModal::detachAllChildren()
{
  IGraphics* g = GetUI();
  if (!g) return;
  for (T3kButton* b : mActionBtns) if (b) g->RemoveControl(b);
  mActionBtns.clear();
  for (T3kButton* b : mPickBtns)   if (b) g->RemoveControl(b);
  mPickBtns.clear();
  if (mCloseBtn) {
    g->RemoveControl(mCloseBtn);
    mCloseBtn = nullptr;
  }
}

void T3kDetailModal::Hide(bool hide)
{
  IControl::Hide(hide);
  if (mCloseBtn) mCloseBtn->Hide(hide);
  for (T3kButton* b : mActionBtns) if (b) b->Hide(hide);
  for (T3kButton* b : mPickBtns)   if (b) b->Hide(hide);
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

  // If we don't have a local path but DO have a URL, fetch it via the
  // ThumbnailCache. First Draw kicks the request; subsequent paints
  // pick up mThumbPath when the worker fires the callback.
  if (!mData.imageUrl.empty() && mData.imagePath.empty()
      && !mThumbRequested && !mThumbLoadFailed) {
    mThumbRequested = true;
    ::t3k::cloud::ThumbnailCache::instance().fetch(
        mData.imageUrl,
        [this](const std::string& path, bool ok) {
          if (ok && !path.empty()) mThumbPath = path;
          else mThumbLoadFailed = true;
          this->SetDirty(false);
        });
  }
  const std::string& effPath =
      !mData.imagePath.empty() ? mData.imagePath : mThumbPath;
  if (!mBitmapLoaded && !mBitmapLoadFailed && !effPath.empty()) {
    try {
      mBitmap = g.LoadBitmap(effPath.c_str(), 1, false);
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

  // Title — 2026-05-25: wrap to at most 2 lines so long Cloud-tab
  // titles (~50+ chars) stay inside the text rect instead of spilling
  // past the right edge of the card. The 14 px/char estimate matches
  // 28pt bold Inter; if the title still doesn't fit in 2 lines, the
  // second line gets ellipsized.
  constexpr float kTitleLineH       = 32.f;
  constexpr size_t kTitleMaxLines    = 2;
  constexpr float kTitlePxPerChar   = 14.f;
  const std::string titleSrc =
      mData.title.empty() ? std::string("(no title)") : mData.title;
  std::vector<std::string> titleLines =
      wrapText(titleSrc, tR - tL, kTitlePxPerChar);
  if (titleLines.size() > kTitleMaxLines) {
    titleLines.resize(kTitleMaxLines);
    if (!titleLines.back().empty()) titleLines.back() += "\xE2\x80\xA6";
  }
  if (titleLines.empty()) titleLines.push_back(titleSrc);
  const float titleBlockH =
      static_cast<float>(titleLines.size()) * kTitleLineH;
  const IRECT titleR(tL, ty, tR, ty + titleBlockH);
  for (size_t li = 0; li < titleLines.size(); ++li) {
    const IRECT lineR(tL, ty + li * kTitleLineH,
                      tR, ty + (li + 1) * kTitleLineH);
    g.DrawText(IText(28.f, th::kText, th::kFontBodyBold,
                     EAlign::Near, EVAlign::Top),
               titleLines[li].c_str(), lineR);
  }
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

  // Versions list (Library) — replaces Makes-and-Models when present.
  // Each row: variant label on the left, PICK button on the right.
  if (!mData.pickables.empty()) {
    const IRECT hdrR(tL, ty, tR, ty + 18.f);
    g.DrawText(IText(14.f, th::kText, th::kFontBodyBold,
                     EAlign::Near, EVAlign::Top),
               "Versions", hdrR);
    ty = hdrR.B + 6.f;

    // 2026-05-25 — scrollable Versions list. The previous version
    // showed only the first 6 pickables and surfaced a "+N more..."
    // text that wasn't actionable — so variants 7+ were unreachable.
    // Now: render ALL pickables in a virtual stack, scrolled by
    // mPickablesScrollOffset; only rows whose full row-rect lies
    // inside the area get drawn (and their PICK button shown). The
    // mouse wheel adjusts the offset (see OnMouseWheel below).
    const float rowH       = 30.f;
    const float pickBtnW   = 64.f;
    const float pickBtnH   = 24.f;
    const size_t count     = mData.pickables.size();

    // The pickables area spans from the current ty cursor down to
    // the bottom of mTextRect (which already excludes the action-
    // button row at the very bottom). Saved in members so the
    // OnMouseWheel handler can hit-test and bound the offset.
    mPickablesAreaRect      = IRECT(tL, ty, tR, mTextRect.B);
    mPickablesContentHeight = static_cast<float>(count) * rowH;

    // Re-clamp the persisted offset in case the area shrank or the
    // pickable set changed since the last wheel event.
    const float maxOffset =
        std::max(0.f, mPickablesContentHeight - mPickablesAreaRect.H());
    if (mPickablesScrollOffset > maxOffset) mPickablesScrollOffset = maxOffset;
    if (mPickablesScrollOffset < 0.f)       mPickablesScrollOffset = 0.f;

    const float areaTop    = mPickablesAreaRect.T;
    const float areaBottom = mPickablesAreaRect.B;

    for (size_t i = 0; i < count; ++i) {
      const float rowTop    = areaTop + static_cast<float>(i) * rowH
                              - mPickablesScrollOffset;
      const float rowBottom = rowTop + rowH;
      const bool fullyInside =
          rowTop >= areaTop - 0.5f && rowBottom <= areaBottom + 0.5f;
      if (!fullyInside) {
        // Off-screen row — hide the PICK button so it can't be
        // clicked outside the visible area.
        if (i < mPickBtns.size() && mPickBtns[i]) mPickBtns[i]->Hide(true);
        continue;
      }
      // 2026-05-26 — leave a 14px left gutter so labels don't sit on
      // top of the (now-left-aligned) scrollbar. 14 = 4 track + 2 left
      // padding + 8 visual breathing.
      const IRECT rowR(tL, rowTop, tR, rowBottom);
      const IRECT labelR(rowR.L + 14.f, rowR.T,
                         rowR.R - pickBtnW - 8.f, rowR.B);
      g.DrawText(IText(13.f, th::kTextMuted, th::kFontBody,
                       EAlign::Near, EVAlign::Middle),
                 mData.pickables[i].label.c_str(), labelR);
      if (i < mPickBtns.size() && mPickBtns[i]) {
        mPickBtns[i]->Hide(false);
        const float by = rowR.MH() - pickBtnH * 0.5f;
        const IRECT btnR(rowR.R - pickBtnW, by,
                         rowR.R,             by + pickBtnH);
        mPickBtns[i]->SetTargetAndDrawRECTs(btnR);
      }
    }

    // Sleek scrollbar — a thin 4px track on the LEFT edge of the
    // variants area (2026-05-26: was on the right; it overlapped the
    // PICK buttons there). Rounded thumb height = viewport fraction
    // of total content. Visible only when there's overflow.
    if (mPickablesContentHeight > mPickablesAreaRect.H() + 1.f) {
      constexpr float kTrackW   = 4.f;
      constexpr float kThumbMin = 24.f;
      const float trackX = mPickablesAreaRect.L + 2.f;
      const IRECT trackR(trackX, areaTop,
                         trackX + kTrackW, areaBottom);
      const float visibleFrac =
          std::min(1.f, mPickablesAreaRect.H() / mPickablesContentHeight);
      const float thumbH =
          std::max(kThumbMin, trackR.H() * visibleFrac);
      const float maxOffset2 =
          std::max(1.f, mPickablesContentHeight - mPickablesAreaRect.H());
      const float scrollFrac =
          std::clamp(mPickablesScrollOffset / maxOffset2, 0.f, 1.f);
      const float thumbY =
          trackR.T + (trackR.H() - thumbH) * scrollFrac;
      mScrollbarThumbRect = IRECT(trackR.L, thumbY,
                                  trackR.R, thumbY + thumbH);

      // Subtle, theme-aware visuals — track translucent dim,
      // thumb slightly brighter (or muted-text on hover/drag-soon).
      const IColor trackColor = th::kBorder;
      const IColor thumbColor =
          mScrollbarDragging ? th::kText : th::kTextMuted;
      g.FillRoundRect(trackColor, trackR, kTrackW * 0.5f);
      g.FillRoundRect(thumbColor, mScrollbarThumbRect, kTrackW * 0.5f);
    } else {
      // No overflow — clear thumb rect so hit tests miss it.
      mScrollbarThumbRect = IRECT();
    }

    ty = areaBottom + 10.f;
  } else if (!mData.makesModels.empty()) {
    // Makes and Models — Cloud path (descriptive, not pickable).
    const IRECT hdrR(tL, ty, tR, ty + 18.f);
    g.DrawText(IText(14.f, th::kText, th::kFontBodyBold,
                     EAlign::Near, EVAlign::Top),
               "Makes and Models", hdrR);
    ty = hdrR.B + 6.f;
    const size_t maxItems = 4;
    const size_t shown = std::min(mData.makesModels.size(), maxItems);
    for (size_t i = 0; i < shown; ++i) {
      const IRECT lineR(tL, ty, tR, ty + kBodyLineH);
      g.DrawText(IText(13.f, th::kTextMuted, th::kFontBody,
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
