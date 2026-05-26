// T3kFocusedSlot.cpp — see T3kFocusedSlot.h.

#include "T3kFocusedSlot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "IGraphics.h"

#include "../theme.h"
#include "../text_util.h"
#include "../controls/T3kReadout.h"
#include "../controls/T3kVMeter.h"
#include "../controls/T3kSectionHeader.h"
#include "../controls/T3kKnob.h"

#include "../../cloud/ThumbnailCache.h"
#include "../../NeuralAmpModeler.h"

namespace t3k::ui {

using namespace iplug::igraphics;

namespace {

// Word-wrap by character budget. Mirrors the helper in T3kDetailModal —
// inlined here to avoid widening that view's public surface. `pxPerChar`
// is a coarse Inter-Regular estimate (~7 px at 14 pt). Maxes out at
// `maxLines`; surplus content gets an ASCII "..." appended.
std::vector<std::string> wrapByWidth(const std::string& text,
                                     float widthPx,
                                     float pxPerChar,
                                     size_t maxLines)
{
  std::vector<std::string> lines;
  if (text.empty()) return lines;
  const size_t maxCharsPerLine = std::max<size_t>(
      8, static_cast<size_t>(widthPx / pxPerChar));

  size_t i = 0;
  std::string line;
  while (i < text.size() && lines.size() < maxLines) {
    const size_t sp = text.find(' ', i);
    const size_t wend = (sp == std::string::npos) ? text.size() : sp;
    std::string word = text.substr(i, wend - i);
    while (word.size() > maxCharsPerLine) {
      const std::string chunk = word.substr(0, maxCharsPerLine);
      word.erase(0, maxCharsPerLine);
      if (!line.empty()) {
        lines.push_back(std::move(line));
        line.clear();
        if (lines.size() >= maxLines) break;
      }
      lines.push_back(chunk);
      if (lines.size() >= maxLines) break;
    }
    if (lines.size() >= maxLines) break;
    if (line.empty()) {
      line = std::move(word);
    } else if (line.size() + 1 + word.size() <= maxCharsPerLine) {
      line += " ";
      line += word;
    } else {
      lines.push_back(std::move(line));
      line = std::move(word);
    }
    i = (sp == std::string::npos) ? text.size() : sp + 1;
  }
  if (!line.empty() && lines.size() < maxLines) lines.push_back(std::move(line));
  if (i < text.size() && !lines.empty()) {
    // Overflow — trim and ellipsize the last line so the reader knows
    // there's more.
    std::string& last = lines.back();
    while (!last.empty() && last.size() + 3 > maxCharsPerLine) last.pop_back();
    last += "...";
  }
  return lines;
}

}  // namespace

T3kFocusedSlot::T3kFocusedSlot(const IRECT& bounds, NeuralAmpModeler& plugin)
: IControl(bounds)
, mPlugin(plugin)
{
}

void T3kFocusedSlot::OnResize()
{
  // 2026-05-26 polish-pass — sizing scaled ~1.4× from the 720 px v6 mockup.
  //   image column: 180 → 240
  //   title band  : 60  → 80 (room for the now-26px h2 + 13px sub-line)
  //   meters col  : 56  → 80 (room for wider 14 px bars + bigger numerals)
  //   readout col : 110 → 150 (so the 36 px yellow numerals don't collide
  //                            with the title at sub-1024 px windows)
  const float pad = 18.f;
  const float gap = 18.f;
  const IRECT inner = mRECT.GetPadded(-pad);

  const float imageW = std::min(320.f, std::max(280.f, inner.W() * 0.28f));
  mImageRect = IRECT(inner.L, inner.T, inner.L + imageW, inner.B);
  mBodyRect  = IRECT(mImageRect.R + gap, inner.T, inner.R, inner.B);

  const float titleH = 80.f;
  mTitleRect   = IRECT(mBodyRect.L, mBodyRect.T, mBodyRect.R, mBodyRect.T + titleH);
  mReadoutRect = IRECT(mTitleRect.R - 150.f, mTitleRect.T, mTitleRect.R, mTitleRect.B);
  mMidRect     = IRECT(mBodyRect.L, mTitleRect.B + 10.f, mBodyRect.R, mBodyRect.B);

  const float metersW = 80.f;
  const float midGap  = 14.f;
  const float availableColsW = (mMidRect.W() - metersW - 2.f * midGap);
  const float infoW = availableColsW * 0.66f;
  const float settingsW = availableColsW - infoW;
  mInfoColRect     = IRECT(mMidRect.L, mMidRect.T,
                           mMidRect.L + infoW, mMidRect.B);
  mSettingsColRect = IRECT(mInfoColRect.R + midGap, mMidRect.T,
                           mInfoColRect.R + midGap + settingsW, mMidRect.B);
  mMetersColRect   = IRECT(mSettingsColRect.R + midGap, mMidRect.T,
                           mMidRect.R, mMidRect.B);

  if (mReadout) mReadout->SetTargetAndDrawRECTs(mReadoutRect);
  if (mInfoHeader) {
    mInfoHeader->SetTargetAndDrawRECTs(
        IRECT(mInfoColRect.L, mInfoColRect.T,
              mInfoColRect.R, mInfoColRect.T + 18.f));
  }
  if (mSettingsHeader) {
    mSettingsHeader->SetTargetAndDrawRECTs(
        IRECT(mSettingsColRect.L, mSettingsColRect.T,
              mSettingsColRect.R, mSettingsColRect.T + 18.f));
  }

  // 2x3 knob grid below the SETTINGS header. The T3kKnob diameter is
  // capped to 64 (r = 32) inside the control; cells smaller than that
  // continue to scale the ring down to fit. We size the cell at 84 px
  // tall so the now-bigger ring (32 r) breathes above its label strip.
  const float knobTop = mSettingsColRect.T + 24.f;
  const float knobH   = (mSettingsColRect.B - knobTop) / 2.f;
  const float knobW   = mSettingsColRect.W() / 3.f;
  auto place = [&](T3kKnob* k, int row, int col) {
    if (!k) return;
    const float x = mSettingsColRect.L + col * knobW;
    const float y = knobTop + row * knobH;
    k->SetTargetAndDrawRECTs(IRECT(x, y, x + knobW, y + knobH));
  };
  place(mKnobBass,   0, 0);
  place(mKnobMids,   0, 1);
  place(mKnobTreble, 0, 2);
  place(mKnobInput,  1, 0);
  place(mKnobOutput, 1, 1);
  place(mKnobDryWet, 1, 2);

  const float halfW = (mMetersColRect.W() - 10.f) * 0.5f;
  if (mMeterIn) {
    mMeterIn->SetTargetAndDrawRECTs(
        IRECT(mMetersColRect.L, mMetersColRect.T,
              mMetersColRect.L + halfW, mMetersColRect.B));
  }
  if (mMeterOut) {
    mMeterOut->SetTargetAndDrawRECTs(
        IRECT(mMetersColRect.R - halfW, mMetersColRect.T,
              mMetersColRect.R,         mMetersColRect.B));
  }
}

void T3kFocusedSlot::OnAttached()
{
  rebuild();
  OnResize();
  // Match the parent's visibility — without this the children paint over the
  // tab body before the first tab-switch.
  Hide(IsHidden());
}

void T3kFocusedSlot::rebuild()
{
  IGraphics* g = GetUI();
  if (!g) return;
  const IRECT ph(0.f, 0.f, 1.f, 1.f);

  mReadout        = new T3kReadout(ph);                     g->AttachControl(mReadout);
  mInfoHeader     = new T3kSectionHeader(ph, "MODEL INFO"); g->AttachControl(mInfoHeader);
  mSettingsHeader = new T3kSectionHeader(ph, "SETTINGS");   g->AttachControl(mSettingsHeader);

  mKnobBass   = new T3kKnob(ph, ::kToneBass,    "BASS");    g->AttachControl(mKnobBass);
  mKnobMids   = new T3kKnob(ph, ::kToneMid,     "MIDS");    g->AttachControl(mKnobMids);
  mKnobTreble = new T3kKnob(ph, ::kToneTreble,  "TREBLE");  g->AttachControl(mKnobTreble);
  mKnobInput  = new T3kKnob(ph, ::kInputLevel,  "INPUT");   g->AttachControl(mKnobInput);
  mKnobOutput = new T3kKnob(ph, ::kOutputLevel, "OUTPUT");  g->AttachControl(mKnobOutput);
  mKnobDryWet = new T3kKnob(ph, ::kDryWet,      "DRY/WET"); g->AttachControl(mKnobDryWet);

  // 2026-05-26 (Phase G1) — touch / drag any focused-panel knob and the
  // big yellow T3kReadout in the title row shows that knob's live value.
  // Captured `label` is a const char* literal from the T3kKnob ctor and
  // outlives the lambda; capturing by value keeps the pointer stable
  // even if the knob is later destroyed.
  auto wire = [this](T3kKnob* k, const char* label) {
    if (!k) return;
    k->setOnTouchOrChange([this, label](T3kKnob* knob) {
      if (auto* p = knob->GetParam())
      {
        char buf[16];
        if (std::strcmp(label, "INPUT") == 0 || std::strcmp(label, "OUTPUT") == 0)
          std::snprintf(buf, sizeof(buf), "%+.1f", p->Value());
        else
          std::snprintf(buf, sizeof(buf), "%.1f", p->Value());
        this->setActiveReadout(label, buf);
      }
    });
  };
  wire(mKnobBass,   "BASS");
  wire(mKnobMids,   "MIDS");
  wire(mKnobTreble, "TREBLE");
  wire(mKnobInput,  "INPUT");
  wire(mKnobOutput, "OUTPUT");
  wire(mKnobDryWet, "DRY/WET");

  // 2026-05-26 (Phase G2) — attach the meters with the input/output ctrl
  // tags so iPlug2's SendControlMsgFromDelegate (fired every ProcessBlock
  // by mInputSender/mOutputSender in NeuralAmpModeler::ProcessBlock) lands
  // directly in each meter's OnMsgFromDelegate decoder. No middleman view
  // hop; the bar updates at audio-rate (delta-gated by setLevel's epsilon).
  mMeterIn  = new T3kVMeter(ph, T3kVMeter::Label::In);
  g->AttachControl(mMeterIn,  ::kCtrlTagInputMeter);
  mMeterOut = new T3kVMeter(ph, T3kVMeter::Label::Out);
  g->AttachControl(mMeterOut, ::kCtrlTagOutputMeter);
}

void T3kFocusedSlot::setSnapshot(ModelInfoSnapshot s)
{
  const bool imageChanged = (s.imagePath != mSnap.imagePath) ||
                            (s.imageUrl  != mSnap.imageUrl);
  mSnap = std::move(s);
  mHasSnapshot = true;
  Hide(IsHidden());
  if (imageChanged) {
    mBitmap            = iplug::igraphics::IBitmap();
    mBitmapLoaded      = false;
    mBitmapLoadFailed  = false;
    mLoadedImagePath.clear();
    mThumbRequested    = false;
    mThumbForUrl.clear();
    mThumbPath.clear();
    mThumbLoadFailed   = false;
  }
  SetDirty(false);
}

void T3kFocusedSlot::clear()
{
  mSnap = {};
  mHasSnapshot = false;
  mBitmap = iplug::igraphics::IBitmap();
  mBitmapLoaded = false;
  mBitmapLoadFailed = false;
  mLoadedImagePath.clear();
  mThumbRequested = false;
  mThumbForUrl.clear();
  mThumbPath.clear();
  mThumbLoadFailed = false;
  Hide(IsHidden());
  SetDirty(false);
}

void T3kFocusedSlot::setActiveReadout(std::string paramName, std::string formattedValue)
{
  if (mReadout) mReadout->setActive(std::move(paramName), std::move(formattedValue));
}

void T3kFocusedSlot::Hide(bool hide)
{
  IControl::Hide(hide);
  const bool hideChildren = hide || !mHasSnapshot;
  if (mReadout)        mReadout->Hide(hideChildren);
  if (mInfoHeader)     mInfoHeader->Hide(hideChildren);
  if (mSettingsHeader) mSettingsHeader->Hide(hideChildren);
  for (auto* k : { mKnobBass, mKnobMids, mKnobTreble,
                   mKnobInput, mKnobOutput, mKnobDryWet }) {
    if (k) k->Hide(hideChildren);
  }
  if (mMeterIn)  mMeterIn->Hide(hideChildren);
  if (mMeterOut) mMeterOut->Hide(hideChildren);
}

void T3kFocusedSlot::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Panel surface.
  g.FillRoundRect(IColor(255, 7, 7, 7), mRECT, 6.f);
  g.DrawRoundRect(IColor(255, 22, 22, 22), mRECT, 6.f, nullptr, 1.f);

  if (!mHasSnapshot)
  {
    const IText h2(30.f, th::kText, th::kFontBodyBold,
                   EAlign::Center, EVAlign::Middle);
    g.DrawText(h2, "Get started by loading a model",
               IRECT(mRECT.L, mRECT.MH() - 38.f, mRECT.R, mRECT.MH()));
    const IText sub(17.f, IColor(255, 190, 190, 190), th::kFontBody,
                    EAlign::Center, EVAlign::Middle);
    g.DrawText(sub, "Welcome to T3K PLAYER",
               IRECT(mRECT.L, mRECT.MH() + 4.f, mRECT.R, mRECT.MH() + 32.f));
    return;
  }

  // ── Image — portrait, cover-cropped to mImageRect ────────────────────
  //
  // Two sources:
  //   1. mSnap.imagePath — absolute local file (sibling .jpg next to the
  //      .nam/.wav). Preferred when present.
  //   2. mSnap.imageUrl  — remote URL from the cloud catalog. Resolved
  //      via cloud::ThumbnailCache (disk-cached HTTP fetch); first Draw
  //      kicks the request and the callback flips mThumbPath on
  //      success, after which the next Draw picks up the local file.
  //
  // The previous redesign only honored (1) — remote-only models painted
  // the placeholder gradient forever. Port the cache-fetch from the
  // pre-X2 T3kModelInfoPane.
  if (mSnap.imagePath.empty()
      && !mSnap.imageUrl.empty()
      && !mThumbLoadFailed
      && mThumbForUrl != mSnap.imageUrl)
  {
    mThumbRequested = true;
    mThumbForUrl    = mSnap.imageUrl;
    ::t3k::cloud::ThumbnailCache::instance().fetch(
        mSnap.imageUrl,
        [this](const std::string& path, bool ok) {
          if (ok && !path.empty()) mThumbPath = path;
          else mThumbLoadFailed = true;
          this->SetDirty(false);
        });
  }

  const std::string effPath =
      !mSnap.imagePath.empty() ? mSnap.imagePath : mThumbPath;

  if (!mBitmapLoaded && !mBitmapLoadFailed && !effPath.empty()
      && mLoadedImagePath != effPath)
  {
    mBitmap = g.LoadBitmap(effPath.c_str());
    mBitmapLoaded = mBitmap.IsValid();
    if (!mBitmapLoaded) mBitmapLoadFailed = true;
    mLoadedImagePath = effPath;
  }

  // Placeholder fill — always paint the surface so the image column has
  // depth even before the bitmap lands.
  g.FillRoundRect(IColor(255, 30, 30, 30), mImageRect, 5.f);

  if (mBitmapLoaded)
  {
    const float bw = static_cast<float>(mBitmap.W());
    const float bh = static_cast<float>(mBitmap.H());
    if (bw > 0.f && bh > 0.f) {
      const float scale = std::max(mImageRect.W() / bw, mImageRect.H() / bh);
      const float dstW  = bw * scale;
      const float dstH  = bh * scale;
      const float dstL  = mImageRect.MW() - dstW * 0.5f;
      const float dstT  = mImageRect.MH() - dstH * 0.5f;
      g.PathClipRegion(mImageRect);
      g.DrawFittedBitmap(mBitmap, IRECT(dstL, dstT, dstL + dstW, dstT + dstH));
      g.PathClipRegion();
    }
  }

  // ── Title row ────────────────────────────────────────────────────────
  // 2026-05-26 polish-pass — h2 20 → 26, sub-line 9 → 12 to match the
  // v6 mockup at the plug-in's real canvas size.
  //
  // Title source strings come from LibraryDb / catalog responses and can
  // include U+2014 em dashes ("Klon '94 — centaur clone"). The vendored
  // Inter subset doesn't carry that glyph, so we pipe the text through
  // text_util::toAsciiSafe before drawing to avoid tofu boxes.
  const IText h2(26.f, th::kText, th::kFontBodyBold,
                 EAlign::Near, EVAlign::Top);
  const std::string safeTitle = ::t3k::text_util::toAsciiSafe(mSnap.displayName);
  g.DrawText(h2, safeTitle.c_str(),
             IRECT(mTitleRect.L, mTitleRect.T,
                   mReadoutRect.L - 8.f, mTitleRect.T + 32.f));

  // Sub-line: creator - format (NAM / IR — reused as the gear-type label).
  // The separator is plain " - " (ASCII) because the Inter subset doesn't
  // carry U+00B7 either; same toAsciiSafe pass scrubs the creator string.
  const IText sub(14.f, IColor(255, 190, 190, 190), th::kFontBody,
                  EAlign::Near, EVAlign::Top);
  std::string subStr = ::t3k::text_util::toAsciiSafe(mSnap.creator);
  if (!mSnap.format.empty()) {
    if (!subStr.empty()) subStr += " - ";
    subStr += mSnap.format;
  }
  g.DrawText(sub, subStr.c_str(),
             IRECT(mTitleRect.L, mTitleRect.T + 38.f,
                   mReadoutRect.L - 8.f, mTitleRect.T + 56.f));

  // ── MODEL INFO column: description on top, tags pinned to bottom ─────
  //
  // 2026-05-26 polish-pass — font 11.5 → 14, and ACTUALLY wrap the
  // description by line. The pre-polish render passed the whole string
  // to a single DrawText, which painted one long line that overflowed
  // the column. Use the wrapByWidth helper to break by space at a coarse
  // char budget, ellipsizing overflow past kDescMaxLines.
  constexpr float  kDescFontPx   = 14.f;
  constexpr float  kDescLineH    = 17.f;
  constexpr float  kDescPxPerChar = 7.2f;

  const IRECT descR(mInfoColRect.L, mInfoColRect.T + 22.f,
                    mInfoColRect.R,
                    mSnap.tags.empty() ? mInfoColRect.B : mInfoColRect.B - 32.f);
  const IText descT(kDescFontPx, IColor(255, 170, 170, 170), th::kFontBody,
                    EAlign::Near, EVAlign::Top);
  const std::string safeDesc = ::t3k::text_util::toAsciiSafe(mSnap.description);
  const size_t maxDescLines = std::max<size_t>(
      1, static_cast<size_t>(std::floor(descR.H() / kDescLineH)));
  const auto descLines = wrapByWidth(safeDesc, descR.W(), kDescPxPerChar,
                                     maxDescLines);
  for (size_t li = 0; li < descLines.size(); ++li) {
    const IRECT lineR(descR.L, descR.T + li * kDescLineH,
                      descR.R, descR.T + (li + 1) * kDescLineH);
    if (lineR.T > descR.B) break;
    g.DrawText(descT, descLines[li].c_str(), lineR);
  }

  const IRECT tagsR(mInfoColRect.L, mInfoColRect.B - 24.f,
                    mInfoColRect.R, mInfoColRect.B);
  const IText tagT(9.f, IColor(255, 204, 204, 204), th::kFontBody,
                   EAlign::Near, EVAlign::Middle);
  float tagX = tagsR.L;
  for (const auto& t : mSnap.tags)
  {
    const std::string safeTag = ::t3k::text_util::toAsciiSafe(t);
    // Approximate auto-size from char count (no measureText available here).
    const float w = 8.f + 6.f * static_cast<float>(safeTag.size());
    const IRECT cR(tagX, tagsR.T + 4.f, tagX + w, tagsR.B - 4.f);
    g.FillRoundRect(IColor(255, 13, 13, 13), cR, cR.H() * 0.5f);
    g.DrawRoundRect(IColor(255, 34, 34, 34), cR, cR.H() * 0.5f, nullptr, 1.f);
    g.DrawText(tagT, safeTag.c_str(), cR);
    tagX += w + 5.f;
    if (tagX > tagsR.R) break;
  }
}

}  // namespace t3k::ui
