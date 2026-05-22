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

  // Static composition helpers — for callers that want to render a gear
  // icon inline without attaching a T3kGearIcon child IControl (e.g.,
  // T3kSlot inlines its icon to avoid IGraphics parent/child plumbing).
  // The caller owns the ISVG cache and invalidates it when the type
  // changes.
  static const char* filenameFor(GearType t);
  static float       aspectFor(GearType t);
  static void        drawInto(iplug::igraphics::IGraphics& g,
                              iplug::igraphics::ISVG& svg,
                              GearType type,
                              const iplug::igraphics::IRECT& bounds);

 private:
  GearType mType;
  // mSvg is std::nullopt until first Draw, then holds the loaded ISVG for
  // mType. setType() resets it so the next Draw reloads the right file.
  std::optional<iplug::igraphics::ISVG> mSvg;
};

}  // namespace t3k::ui
