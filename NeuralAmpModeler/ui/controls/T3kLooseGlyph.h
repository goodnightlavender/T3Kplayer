// T3kLooseGlyph — bare-icon button used for the loose undo/redo icons in
// the v6 header. Phase 2b, Decision 42.
//
// No background, no border. Just a single SVG glyph rendered at the
// caller-provided bounds. The earlier revision drew Unicode U+21B6 / U+21B7
// as text in Inter-Regular, but Inter's vendored subset doesn't ship those
// curved-arrow code points — they rendered as tofu boxes. SVGs solve that
// (their stroke color is baked into the file at #cfcfcf, which sits between
// kTextMuted and kText so the default state reads correctly; we don't
// retint on hover — the SVG path doesn't expose stroke override cleanly in
// this iPlug2 revision, so hover state is communicated by the cursor
// changing only). Disabled state dims by drawing the SVG at lower alpha
// via a translucent rect overlay.

#pragma once

#include <functional>
#include <optional>
#include <string>

#include "IControl.h"
#include "IGraphicsStructs.h"  // for ISVG
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::ISVG;
using ::iplug::igraphics::IMouseMod;

class T3kLooseGlyph : public IControl
{
public:
  T3kLooseGlyph(const IRECT& bounds,
                const char* svgFilename,
                std::function<void()> onClick,
                bool disabled = false);

  void setDisabled(bool d);
  bool disabled() const { return mDisabled; }

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const IMouseMod& mod) override;
  void OnMouseOut() override;

private:
  std::string mSvgFilename;
  // ISVG has no default constructor — populated on first Draw, when
  // IGraphics is guaranteed alive. Same lazy-load pattern as T3kLogo.
  std::optional<ISVG> mSvg;
  std::function<void()> mOnClick;
  bool mDisabled;
};

}  // namespace t3k::ui
