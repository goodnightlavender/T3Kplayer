#include "T3kSectionHeader.h"

#include <cctype>
#include "IGraphics.h"
#include "../theme.h"

namespace t3k::ui {

using namespace iplug::igraphics;

T3kSectionHeader::T3kSectionHeader(const IRECT& bounds, std::string label)
: IControl(bounds)
, mLabel(std::move(label))
{
  SetIgnoreMouse(true);
  for (char& c : mLabel)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

void T3kSectionHeader::Draw(IGraphics& g)
{
  namespace th = ::t3k::theme;
  const IText t(9.f, th::kAccent, th::kFontBodyBold,
                EAlign::Near, EVAlign::Top);
  g.DrawText(t, mLabel.c_str(), mRECT);
  g.FillRect(IColor(255, 29, 29, 29),
             IRECT(mRECT.L, mRECT.B - 1.f, mRECT.R, mRECT.B));
}

}  // namespace t3k::ui
