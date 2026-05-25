// T3kDownloadsPill — header-right downloads surface (Polish 3c).
//
// Sits immediately to the left of T3kPresetPill. Renders as a rounded
// pill with a downward-arrow glyph and a small integer badge showing
// the in-flight download count (Queued / Listing / Downloading /
// Writing). Done / Failed items don't count toward the badge but are
// still surfaced in the popover until pruned.
//
// Subscribes to ::t3k::cloud::Downloader in OnAttached. The listener
// callback fires on the HTTP worker thread; we stash a snapshot of
// every status we've seen under mMtx and SetDirty(false). The next
// paint pulls a copy under the mutex.
//
// Clicking the pill toggles a popover anchored under it. The popover
// is laid out and owned by ToneRoot (matches the preset-pill /
// account-menu pattern); this class only fires onToggleOverlay.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "IControl.h"

#include "../../cloud/Downloader.h"
#include "../theme.h"

namespace t3k::ui {

using ::iplug::igraphics::IControl;
using ::iplug::igraphics::IGraphics;
using ::iplug::igraphics::IRECT;
using ::iplug::igraphics::IMouseMod;

class T3kDownloadsPill : public IControl
{
public:
  // Row snapshot consumed by ToneRoot when building the popover. Cheap
  // copy of the latest DownloadStatus we received per tone_id.
  struct Row {
    int id = 0;                                   // download id
    int tone_id = 0;
    std::string tone_title;
    ::t3k::cloud::DownloadStatus::Stage stage =
        ::t3k::cloud::DownloadStatus::Stage::Queued;
    int model_index = 0;
    int total_models = 0;
    std::string error_message;
  };

  T3kDownloadsPill(const IRECT& bounds,
                   std::function<void()> onToggleOverlay);
  ~T3kDownloadsPill() override;

  // Snapshot of every status we've received. Caller holds no locks.
  std::vector<Row> snapshotRows() const;

  // Active = Queued / Listing / Downloading / Writing.
  int activeCount() const;

  void Draw(IGraphics& g) override;
  void OnAttached() override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const IMouseMod& mod) override;
  void OnMouseOut() override;

private:
  std::function<void()> mOnToggleOverlay;

  mutable std::mutex mMtx;
  // Latest status per tone_id. Keyed by tone_id so a tone that already
  // has a download in-flight doesn't double up if the user re-clicks.
  std::unordered_map<int, Row> mRows;

  int mListenerId = 0;
};

}  // namespace t3k::ui
