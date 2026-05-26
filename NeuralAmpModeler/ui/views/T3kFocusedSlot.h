// T3kFocusedSlot — focused-slot panel inside the Tone tab.
//
// Layout (left to right):
//   [image 180 px, portrait, cover-cropped]
//   [body: title row, then mid row = MODEL INFO | SETTINGS | METERS]
//
// Owns: image (drawn inline), title h2 + sub (drawn inline), 1 T3kReadout,
// 6 T3kKnobs (BASS/MIDS/TREBLE, INPUT/OUTPUT/DRY-WET) bound to iPlug
// params, 2 T3kVMeters (IN/OUT), 2 T3kSectionHeaders (MODEL INFO, SETTINGS).
//
// Renders an empty-state placeholder when no slot is selected.

#pragma once

#include <string>

#include "IControl.h"
#include "IGraphicsStructs.h"

// We reuse the ModelInfoSnapshot struct already defined in T3kModelInfoPane.h
// rather than re-defining a near-duplicate. Once T3kModelInfoPane is deleted
// in cleanup (Phase X), the struct will migrate into this header.
#include "T3kModelInfoPane.h"

class NeuralAmpModeler;

namespace t3k::ui {

class T3kReadout;
class T3kVMeter;
class T3kSectionHeader;
class T3kKnob;

class T3kFocusedSlot : public iplug::igraphics::IControl
{
public:
  T3kFocusedSlot(const iplug::igraphics::IRECT& bounds,
                 NeuralAmpModeler& plugin);

  void setSnapshot(ModelInfoSnapshot s);
  void clear();

  void setMeterLevels(double inLevel0to1, double inPeak0to1, double inPeakDb,
                      double outLevel0to1, double outPeak0to1, double outPeakDb);

  void setActiveReadout(std::string paramName, std::string formattedValue);

  void OnResize() override;
  void OnAttached() override;
  void Draw(iplug::igraphics::IGraphics& g) override;
  void Hide(bool hide) override;

private:
  void rebuild();

  NeuralAmpModeler& mPlugin;

  ModelInfoSnapshot mSnap;
  bool mHasSnapshot = false;

  iplug::igraphics::IRECT mImageRect;
  iplug::igraphics::IRECT mBodyRect;
  iplug::igraphics::IRECT mTitleRect;
  iplug::igraphics::IRECT mReadoutRect;
  iplug::igraphics::IRECT mMidRect;
  iplug::igraphics::IRECT mInfoColRect;
  iplug::igraphics::IRECT mSettingsColRect;
  iplug::igraphics::IRECT mMetersColRect;

  iplug::igraphics::IBitmap mBitmap;
  bool mBitmapLoaded = false;
  bool mBitmapLoadFailed = false;
  std::string mLoadedImagePath;  // path the current mBitmap was loaded from

  T3kReadout*       mReadout         = nullptr;
  T3kSectionHeader* mInfoHeader      = nullptr;
  T3kSectionHeader* mSettingsHeader  = nullptr;
  T3kKnob*          mKnobBass        = nullptr;
  T3kKnob*          mKnobMids        = nullptr;
  T3kKnob*          mKnobTreble      = nullptr;
  T3kKnob*          mKnobInput       = nullptr;
  T3kKnob*          mKnobOutput      = nullptr;
  T3kKnob*          mKnobDryWet      = nullptr;
  T3kVMeter*        mMeterIn         = nullptr;
  T3kVMeter*        mMeterOut        = nullptr;
};

}  // namespace t3k::ui
