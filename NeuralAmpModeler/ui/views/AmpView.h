// AmpView.h — Phase 2 thin amp tab view.
//
// AmpView in Phase 2 is intentionally minimal: a recessed waveform-placeholder
// rectangle and a centered "No model loaded" label. The persistent 5-knob row
// (Input / Bass / Mid / Treble / Output) lives in ToneRoot, not here — see
// AmpView-params.notes.md alongside this file for the upstream parameter
// indices ToneRoot consumes.
//
// Phase 2.x will replace the placeholder rectangle with a real waveform / IO
// meter rendering.

#pragma once

#include "IControl.h"

namespace t3k::ui {

class AmpView : public iplug::igraphics::IControl {
public:
  explicit AmpView(const iplug::igraphics::IRECT& bounds);
  void Draw(iplug::igraphics::IGraphics& g) override;
  void OnResize() override;

private:
  iplug::igraphics::IRECT mWaveformRect;
  iplug::igraphics::IRECT mModelNameRect;
};

}  // namespace t3k::ui
