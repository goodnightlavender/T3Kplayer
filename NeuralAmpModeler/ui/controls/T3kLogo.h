// T3kLogo.h
#pragma once
#include "IControl.h"
#include <optional>

namespace t3k::ui {

class T3kLogo : public iplug::igraphics::IControl {
public:
  explicit T3kLogo(const iplug::igraphics::IRECT& bounds);
  void Draw(iplug::igraphics::IGraphics& g) override;

private:
  // ISVG has no default constructor — populate lazily on first Draw,
  // when IGraphics is guaranteed alive.
  std::optional<iplug::igraphics::ISVG> mSvg;
};

}  // namespace t3k::ui
