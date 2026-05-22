// T3kSearchBar — pill-shaped text input with a magnifier glyph at left.
//
// Visual: kBgSurface fill, 1px kBorder outline, kRadiusPill rounding.
// A small circle-with-handle magnifier glyph sits at the left edge; the
// editable area begins to the right of it. While the input is empty, the
// placeholder is drawn in kTextMuted; otherwise the value is drawn in kText.
//
// Input: a click anywhere on the bar opens iPlug2's text-entry overlay.
// Commit (Enter / focus loss) fires the onChanged callback with the new
// value. Esc cancels without firing.

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

class T3kSearchBar : public IControl
{
public:
  T3kSearchBar(const IRECT& bounds,
               std::function<void(const std::string&)> onChanged,
               // Default placeholder uses a real Unicode ellipsis (U+2026).
               const char* placeholder = "Search\xE2\x80\xA6");

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnTextEntryCompletion(const char* str, int valIdx) override;

  const std::string& value() const { return mValue; }

private:
  std::string mValue;
  std::string mPlaceholder;
  std::function<void(const std::string&)> mOnChanged;
};

}  // namespace t3k::ui
