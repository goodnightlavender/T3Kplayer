// T3kLooseGlyph — bare-glyph button used for the loose `↶ ↷` undo/redo
// icons in the v6 header. Phase 2b, Decision 42.
//
// No background, no border. Just a single Unicode glyph rendered in
// kTextMuted (default) or kText (hover), with a kBorder color when
// disabled. Clicking fires onClick unless mDisabled is true.

#pragma once

#include <functional>
#include <string>

#include "IControl.h"
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::IMouseMod;

class T3kLooseGlyph : public IControl
{
public:
  T3kLooseGlyph(const IRECT& bounds,
                const char* glyph,
                std::function<void()> onClick,
                bool disabled = false);

  void setDisabled(bool d);
  bool disabled() const { return mDisabled; }

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const IMouseMod& mod) override;
  void OnMouseOut() override;

private:
  std::string mGlyph;
  std::function<void()> mOnClick;
  bool mDisabled;
};

}  // namespace t3k::ui
