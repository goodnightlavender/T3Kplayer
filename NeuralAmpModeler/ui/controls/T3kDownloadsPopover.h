// T3kDownloadsPopover — anchored panel listing the current downloads.
//
// Sibling control to T3kDownloadsPill (also Polish 3c). The pill owns
// the Downloader subscription and exposes a snapshot via
// snapshotRows(); the popover pulls that snapshot on Draw and renders
// one row per download with title + stage label.
//
// Ownership / z-order: ToneRoot creates the popover after T3kPresetOverlay
// (so it lands above strip tiles + the preset overlay's backdrop) and
// hides it by default. A T3kClickBackdrop sits one slot below it in
// the control list and dismisses on outside-click.

#pragma once

#include <functional>
#include <vector>

#include "IControl.h"

#include "T3kDownloadsPill.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::IMouseMod;

class T3kDownloadsPopover : public IControl
{
public:
  // The provider closure is called on Draw to pull the latest rows
  // from T3kDownloadsPill (which owns the Downloader subscription).
  using RowsProvider = std::function<std::vector<T3kDownloadsPill::Row>()>;

  T3kDownloadsPopover(const IRECT& bounds, RowsProvider provider);

  void Draw(IGraphics& g) override;

private:
  RowsProvider mProvider;
};

}  // namespace t3k::ui
