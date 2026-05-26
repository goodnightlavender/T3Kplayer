#include "T3kReadout.h"

#include <cctype>
#include "IGraphics.h"
#include "../theme.h"

namespace t3k::ui {

using namespace iplug::igraphics;

T3kReadout::T3kReadout(const IRECT& bounds)
: IControl(bounds)
, mValue("--")
{
  SetIgnoreMouse(true);
}

void T3kReadout::setActive(std::string paramName, std::string formattedValue)
{
  if (paramName == mParamName && formattedValue == mValue) return;
  mParamName = std::move(paramName);
  mValue     = std::move(formattedValue);
  SetDirty(false);
}

void T3kReadout::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;
  // Big yellow numerals — right-aligned, ~28 px.
  const IText numText(28.f, th::kAccent, th::kFontBody,
                      EAlign::Far, EVAlign::Top);
  g.DrawText(numText, mValue.c_str(), mRECT);

  // Param-name label below.
  const IRECT lblR(mRECT.L, mRECT.B - 14.f, mRECT.R, mRECT.B);
  const IText lblText(9.f, th::kTextMuted, th::kFontBody,
                      EAlign::Far, EVAlign::Middle);
  std::string upper = mParamName;
  for (char& c : upper)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  g.DrawText(lblText, upper.c_str(), lblR);
}

}  // namespace t3k::ui
