// T3kGearIcon — renders one of five gear-type silhouette SVGs (Decision 47).
//
// Stateless control. Given a GearType, draws the corresponding SVG fitted
// inside mRECT while preserving aspect ratio. Each source SVG ships with
// #cfcfcf strokes; T3kSlot tints by drawing a translucent overlay when
// selected (see Decision 47 — selection state of the slot, not the icon).
//
// The SVG is loaded lazily on first Draw because iPlug2's ISVG has no
// default constructor — IGraphics must be alive (matches T3kLogo's pattern).

#pragma once

#include "IControl.h"
#include <optional>

namespace t3k::ui {

enum class GearType {
  Pedal,
  Amp,
  Cab,
  Outboard,
  FullRig,
};

class T3kGearIcon : public iplug::igraphics::IControl {
 public:
  T3kGearIcon(const iplug::igraphics::IRECT& bounds, GearType type);

  void setType(GearType t);
  GearType type() const { return mType; }

  void Draw(iplug::igraphics::IGraphics& g) override;

 private:
  // Returns the C-string filename macro for the current mType.
  // Defined in .cpp so config.h doesn't bleed into the header.
  const char* svgFilename() const;

  GearType mType;
  // mSvg is std::nullopt until first Draw, then holds the loaded ISVG for
  // mType. setType() resets it so the next Draw reloads the right file.
  std::optional<iplug::igraphics::ISVG> mSvg;
};

}  // namespace t3k::ui
