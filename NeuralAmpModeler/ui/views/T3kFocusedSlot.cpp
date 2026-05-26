// T3kFocusedSlot.cpp — see T3kFocusedSlot.h.

#include "T3kFocusedSlot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "IGraphics.h"

#include "../theme.h"
#include "../controls/T3kReadout.h"
#include "../controls/T3kVMeter.h"
#include "../controls/T3kSectionHeader.h"
#include "../controls/T3kKnob.h"

#include "../../NeuralAmpModeler.h"

namespace t3k::ui {

using namespace iplug::igraphics;

T3kFocusedSlot::T3kFocusedSlot(const IRECT& bounds, NeuralAmpModeler& plugin)
: IControl(bounds)
, mPlugin(plugin)
{
}

void T3kFocusedSlot::OnResize()
{
  const float pad = 14.f;
  const float gap = 14.f;
  const IRECT inner = mRECT.GetPadded(-pad);

  mImageRect = IRECT(inner.L, inner.T, inner.L + 180.f, inner.B);
  mBodyRect  = IRECT(mImageRect.R + gap, inner.T, inner.R, inner.B);

  const float titleH = 60.f;
  mTitleRect   = IRECT(mBodyRect.L, mBodyRect.T, mBodyRect.R, mBodyRect.T + titleH);
  mReadoutRect = IRECT(mTitleRect.R - 110.f, mTitleRect.T, mTitleRect.R, mTitleRect.B);
  mMidRect     = IRECT(mBodyRect.L, mTitleRect.B + 8.f, mBodyRect.R, mBodyRect.B);

  const float metersW = 56.f;
  const float midGap  = 14.f;
  const float colW    = (mMidRect.W() - metersW - 2.f * midGap) * 0.5f;
  mInfoColRect     = IRECT(mMidRect.L, mMidRect.T,
                           mMidRect.L + colW, mMidRect.B);
  mSettingsColRect = IRECT(mInfoColRect.R + midGap, mMidRect.T,
                           mInfoColRect.R + midGap + colW, mMidRect.B);
  mMetersColRect   = IRECT(mSettingsColRect.R + midGap, mMidRect.T,
                           mMidRect.R, mMidRect.B);

  if (mReadout) mReadout->SetTargetAndDrawRECTs(mReadoutRect);
  if (mInfoHeader) {
    mInfoHeader->SetTargetAndDrawRECTs(
        IRECT(mInfoColRect.L, mInfoColRect.T,
              mInfoColRect.R, mInfoColRect.T + 14.f));
  }
  if (mSettingsHeader) {
    mSettingsHeader->SetTargetAndDrawRECTs(
        IRECT(mSettingsColRect.L, mSettingsColRect.T,
              mSettingsColRect.R, mSettingsColRect.T + 14.f));
  }

  // 2x3 knob grid below the SETTINGS header.
  const float knobTop = mSettingsColRect.T + 20.f;
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

  const float halfW = (mMetersColRect.W() - 8.f) * 0.5f;
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
        std::snprintf(buf, sizeof(buf), "%+.1f", p->Value());
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
  mSnap = std::move(s);
  mHasSnapshot = true;
  // Force an image reload if the path changed.
  if (mSnap.imagePath != mLoadedImagePath) {
    mBitmapLoaded     = false;
    mBitmapLoadFailed = false;
  }
  SetDirty(false);
}

void T3kFocusedSlot::clear()
{
  mSnap = {};
  mHasSnapshot = false;
  mBitmapLoaded = false;
  mBitmapLoadFailed = false;
  mLoadedImagePath.clear();
  SetDirty(false);
}

void T3kFocusedSlot::setActiveReadout(std::string paramName, std::string formattedValue)
{
  if (mReadout) mReadout->setActive(std::move(paramName), std::move(formattedValue));
}

void T3kFocusedSlot::Hide(bool hide)
{
  IControl::Hide(hide);
  if (mReadout)        mReadout->Hide(hide);
  if (mInfoHeader)     mInfoHeader->Hide(hide);
  if (mSettingsHeader) mSettingsHeader->Hide(hide);
  for (auto* k : { mKnobBass, mKnobMids, mKnobTreble,
                   mKnobInput, mKnobOutput, mKnobDryWet }) {
    if (k) k->Hide(hide);
  }
  if (mMeterIn)  mMeterIn->Hide(hide);
  if (mMeterOut) mMeterOut->Hide(hide);
}

void T3kFocusedSlot::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;

  // Panel surface.
  g.FillRoundRect(IColor(255, 7, 7, 7), mRECT, 6.f);
  g.DrawRoundRect(IColor(255, 22, 22, 22), mRECT, 6.f, nullptr, 1.f);

  if (!mHasSnapshot)
  {
    const IText t(13.f, th::kTextMuted, th::kFontBody,
                  EAlign::Center, EVAlign::Middle);
    g.DrawText(t, "Click + to load a model", mRECT);
    return;
  }

  // ── Image — portrait, cover-cropped to mImageRect ────────────────────
  if (!mBitmapLoaded && !mBitmapLoadFailed && !mSnap.imagePath.empty())
  {
    mBitmap = g.LoadBitmap(mSnap.imagePath.c_str());
    mBitmapLoaded = mBitmap.IsValid();
    if (!mBitmapLoaded) mBitmapLoadFailed = true;
    mLoadedImagePath = mSnap.imagePath;
  }
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
      g.DrawFittedBitmap(mBitmap, IRECT(dstL, dstT, dstL + dstW, dstT + dstH));
    }
  }

  // ── Title row ────────────────────────────────────────────────────────
  const IText h2(20.f, th::kText, th::kFontBodyBold,
                 EAlign::Near, EVAlign::Top);
  g.DrawText(h2, mSnap.displayName.c_str(),
             IRECT(mTitleRect.L, mTitleRect.T,
                   mReadoutRect.L - 8.f, mTitleRect.T + 24.f));

  // Sub-line: creator · format (NAM / IR — reused as the gear-type label).
  const IText sub(9.f, th::kTextMuted, th::kFontBody,
                  EAlign::Near, EVAlign::Top);
  std::string subStr = mSnap.creator;
  if (!mSnap.format.empty()) {
    if (!subStr.empty()) subStr += " · ";
    subStr += mSnap.format;
  }
  g.DrawText(sub, subStr.c_str(),
             IRECT(mTitleRect.L, mTitleRect.T + 28.f,
                   mReadoutRect.L - 8.f, mTitleRect.T + 40.f));

  // ── MODEL INFO column: description on top, tags pinned to bottom ─────
  const IRECT descR(mInfoColRect.L, mInfoColRect.T + 18.f,
                    mInfoColRect.R, mInfoColRect.B - 28.f);
  const IText descT(11.5f, IColor(255, 170, 170, 170), th::kFontBody,
                    EAlign::Near, EVAlign::Top);
  g.DrawText(descT, mSnap.description.c_str(), descR);

  const IRECT tagsR(mInfoColRect.L, mInfoColRect.B - 24.f,
                    mInfoColRect.R, mInfoColRect.B);
  const IText tagT(9.f, IColor(255, 204, 204, 204), th::kFontBody,
                   EAlign::Near, EVAlign::Middle);
  float tagX = tagsR.L;
  for (const auto& t : mSnap.tags)
  {
    // Approximate auto-size from char count (no measureText available here).
    const float w = 8.f + 6.f * static_cast<float>(t.size());
    const IRECT cR(tagX, tagsR.T + 4.f, tagX + w, tagsR.B - 4.f);
    g.FillRoundRect(IColor(255, 13, 13, 13), cR, cR.H() * 0.5f);
    g.DrawRoundRect(IColor(255, 34, 34, 34), cR, cR.H() * 0.5f, nullptr, 1.f);
    g.DrawText(tagT, t.c_str(), cR);
    tagX += w + 5.f;
    if (tagX > tagsR.R) break;
  }
}

}  // namespace t3k::ui
