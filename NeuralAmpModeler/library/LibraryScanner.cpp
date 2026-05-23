// LibraryScanner.cpp — implementation. See LibraryScanner.h.
//
// The walk is a single `recursive_directory_iterator` pass. For every
// file with a known extension we ask ModelSidecar to parse the sibling
// JSON; the resulting ModelMeta goes straight into LibraryDb::upsertModel
// and we post a ModelAdded (or ModelUpdated — we don't distinguish in
// Phase 3) event. At the end of the pass we mark every previously-known
// row whose (tone_id, model_id) wasn't seen as missing=1.

#include "LibraryScanner.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "EventBus.h"
#include "LibraryDb.h"
#include "ModelMeta.h"
#include "ModelSidecar.h"
#include "../settings/Settings.h"

namespace t3k::library {

namespace {

namespace fs = std::filesystem;

bool IsModelExtension(const std::string& extLower)
{
  return extLower == ".nam" || extLower == ".wav" ||
         extLower == ".flac" || extLower == ".ogg";
}

std::string AsciiLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

}  // namespace

LibraryScanner& LibraryScanner::instance()
{
  static LibraryScanner s;
  return s;
}

LibraryScanner::~LibraryScanner()
{
  if (mThread.joinable()) {
    // We can't block UI shutdown on a slow walk; detach and trust the
    // process exit to tear it down.
    mThread.detach();
  }
}

void LibraryScanner::rescan(std::optional<std::string> rootOverride)
{
  // One scan at a time. Subsequent requests are dropped (the UI's
  // Rescan button is a no-op while scanning — Phase 3 doesn't queue).
  bool expected = false;
  if (!mRunning.compare_exchange_strong(expected, true)) return;

  std::string root;
  if (rootOverride.has_value()) {
    root = *rootOverride;
  } else {
    root = ::t3k::settings::instance().tone3000_root;
  }

  if (root.empty()) {
    mRunning.store(false);
    return;
  }

  // Reap any previous thread before launching a fresh one.
  if (mThread.joinable()) mThread.join();

  mThread = std::thread([this, root = std::move(root)]() mutable {
    walk(std::move(root));
    mRunning.store(false);
  });
}

void LibraryScanner::walk(std::string root)
{
  EventBus::instance().post(LibraryEvent::ScanStarted, 0);

  std::vector<LibraryDb::ToneModelKey> seen;
  std::error_code ec;
  const fs::path base = fs::u8path(root);

  if (fs::exists(base, ec) && !ec && fs::is_directory(base, ec)) {
    for (auto it = fs::recursive_directory_iterator(
             base, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (ec) {
        ec.clear();
        continue;
      }
      const fs::directory_entry& de = *it;
      if (!de.is_regular_file(ec) || ec) {
        ec.clear();
        continue;
      }
      const fs::path& p = de.path();
      const std::string ext = AsciiLower(p.extension().u8string());
      if (!IsModelExtension(ext)) continue;

      auto meta = loadSidecarFor(p.u8string());
      if (!meta.has_value()) continue;  // no sidecar → ignore

      const int64_t id = LibraryDb::instance().upsertModel(*meta);
      if (id <= 0) continue;

      seen.emplace_back(meta->t3k_tone_id, meta->t3k_model_id);
      // Phase 3 doesn't distinguish add vs update; the UI just refreshes
      // its query when it receives any per-row event.
      EventBus::instance().post(LibraryEvent::ModelAdded, id);
    }
  }

  // Anything that was in the DB but not on disk this pass is now
  // missing. (We don't post per-row ModelRemoved events — UI just
  // re-queries on ScanFinished.)
  LibraryDb::instance().markMissingExcept(seen);

  EventBus::instance().post(LibraryEvent::ScanFinished, 0);
}

}  // namespace t3k::library
