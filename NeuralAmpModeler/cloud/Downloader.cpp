// Downloader.cpp — see Downloader.h.

#include "Downloader.h"

#include "Session.h"
#include "Tone3000Client.h"
#include "../library/EventBus.h"
#include "../library/LibraryDb.h"
#include "../library/ModelMeta.h"
#include "../library/ModelSidecar.h"
#include "../library/Paths.h"
#include "../net/HttpClient.h"
#include "../net/HttpRequest.h"
#include "../net/HttpResponse.h"
#include "../settings/Settings.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace t3k::cloud {

namespace {

namespace fs = std::filesystem;

// Map TONE3000 Platform → file extension for the local copy. nam and
// IR (wav) are the two real cases in 0.1; the others (aida-x, etc.)
// fall through to a neutral .bin so the download still completes
// even if we can't load it yet.
const char* extForPlatform(Platform p)
{
  switch (p) {
    case Platform::Nam:        return ".nam";
    case Platform::Ir:         return ".wav";
    case Platform::AidaX:      return ".json";
    case Platform::AaSnapshot: return ".bin";
    case Platform::Proteus:    return ".bin";
  }
  return ".bin";
}

// Sanitize a server-supplied string for use as a filename. Keeps
// [A-Za-z0-9._-], substitutes space → '_', drops everything else,
// caps at 64 chars.
std::string sanitizeFilename(const std::string& in)
{
  std::string out;
  out.reserve(std::min<std::size_t>(in.size(), 64));
  for (char c : in) {
    if (out.size() >= 64) break;
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) || c == '_' || c == '-' || c == '.') {
      out.push_back(c);
    } else if (c == ' ') {
      out.push_back('_');
    }
  }
  if (out.empty()) out = "model";
  return out;
}

// Map (Tone, Model, localPath) → ModelMeta so we can hand the result
// to LibraryDb and writeSidecarFor.
::t3k::library::ModelMeta buildModelMeta(
    const Tone& tone, const Model& model, const std::string& localPath)
{
  ::t3k::library::ModelMeta m;
  m.uri          = localPath;
  m.filename     = ::t3k::library::pathToUtf8(fs::u8path(localPath).filename());
  m.size_bytes   = 0;  // filled after write completes
  m.mtime        = 0;  // scanner backfills on next walk

  m.t3k_tone_id  = std::to_string(tone.id);
  m.t3k_model_id = std::to_string(model.id);

  // Prefer the per-model name as the display name when it differs
  // from the tone title — this is how multi-variant tones (e.g.
  // "BASSRIG '64 + DCX BOOST" / "BASSRIG '64") show distinct rows
  // in the Library tab instead of three duplicate entries. Falls
  // back to the tone title when the model has no distinct name.
  m.display_name = (!model.name.empty() && model.name != tone.title)
                     ? model.name
                     : tone.title;
  m.t3k_creator     = tone.user.username;
  m.t3k_creator_id  = tone.user.id;
  m.t3k_description = tone.description.value_or("");
  m.t3k_image_url   = (tone.images.has_value() && !tone.images->empty())
                        ? (*tone.images)[0] : std::string{};

  m.gear_type    = toWire(tone.gear);
  if (!tone.makes.empty()) m.make = tone.makes.front().name;
  m.model_name   = model.name;

  for (const auto& tag : tone.tags) m.tags.push_back(tag.name);

  // Kind derived from the on-disk extension.
  const std::string ext = ::t3k::library::pathToUtf8(
      fs::u8path(localPath).extension());
  if (ext == ".nam")                              m.kind = "nam";
  else if (ext == ".wav" || ext == ".flac")       m.kind = "ir";
  else                                            m.kind = "nam";

  return m;
}

}  // namespace

Downloader& Downloader::instance()
{
  static Downloader inst;
  return inst;
}

int Downloader::enqueueTone(const Tone& t)
{
  // Prune long-dead items lazily so the list doesn't grow unbounded.
  {
    std::lock_guard<std::mutex> lk(mItemMtx);
    mItems.erase(
        std::remove_if(mItems.begin(), mItems.end(),
                       [](const std::shared_ptr<Item>& it) {
                         return it->status.stage ==
                                  DownloadStatus::Stage::Done ||
                                it->status.stage ==
                                  DownloadStatus::Stage::Failed;
                       }),
        mItems.end());
  }

  auto it = std::make_shared<Item>();
  it->tone = t;
  it->status.id         = mNextId++;
  it->status.tone_id    = t.id;
  it->status.tone_title = t.title;
  it->status.stage      = DownloadStatus::Stage::Queued;

  {
    std::lock_guard<std::mutex> lk(mItemMtx);
    mItems.push_back(it);
  }
  publish(it->status);

  // Kick the pipeline. listModels fires its completion on the HTTP
  // worker thread; subsequent steps chain on that thread too.
  stepListing(it);
  return it->status.id;
}

void Downloader::cancel(int id)
{
  std::shared_ptr<Item> target;
  {
    std::lock_guard<std::mutex> lk(mItemMtx);
    for (auto& it : mItems) {
      if (it->status.id == id) { target = it; break; }
    }
  }
  if (!target) return;
  target->token.cancel();
  // The in-flight HTTP completion sees `res.canceled == true` and
  // routes through failItem with msg="canceled".
}

int Downloader::subscribe(Listener cb)
{
  const int id = mNextListenerId++;
  std::lock_guard<std::mutex> lk(mListenerMtx);
  mListeners.emplace_back(id, std::move(cb));
  return id;
}

void Downloader::unsubscribe(int id)
{
  std::lock_guard<std::mutex> lk(mListenerMtx);
  mListeners.erase(
      std::remove_if(mListeners.begin(), mListeners.end(),
                     [id](const auto& p) { return p.first == id; }),
      mListeners.end());
}

std::vector<DownloadStatus> Downloader::active() const
{
  std::vector<DownloadStatus> out;
  std::lock_guard<std::mutex> lk(mItemMtx);
  out.reserve(mItems.size());
  for (const auto& it : mItems) out.push_back(it->status);
  return out;
}

void Downloader::publish(const DownloadStatus& s)
{
  // Copy listeners under the mutex so callbacks don't run with the
  // listener mutex held (a callback that calls back into Downloader
  // would deadlock).
  std::vector<Listener> listeners;
  {
    std::lock_guard<std::mutex> lk(mListenerMtx);
    listeners.reserve(mListeners.size());
    for (const auto& p : mListeners) listeners.push_back(p.second);
  }
  for (auto& l : listeners) if (l) l(s);
}

// ── Pipeline stages ─────────────────────────────────────────────────

void Downloader::stepListing(std::shared_ptr<Item> it)
{
  it->status.stage = DownloadStatus::Stage::Listing;
  publish(it->status);

  Tone3000Client::instance().listModels(
      it->tone.id, /*page*/ 1, /*page_size*/ 50,
      [this, it](ModelListResult r) {
        if (!r.success) {
          this->failItem(it, r.error_message.empty()
                              ? std::string("listModels failed")
                              : r.error_message);
          return;
        }
        if (r.data.data.empty()) {
          this->failItem(it, "Tone has no models to download");
          return;
        }
        it->models = std::move(r.data.data);
        it->status.total_models = static_cast<int>(it->models.size());
        publish(it->status);
        this->stepDownload(it, 0);
      });
}

void Downloader::stepDownload(std::shared_ptr<Item> it, int modelIdx)
{
  if (modelIdx >= static_cast<int>(it->models.size())) {
    this->finish(it);
    return;
  }

  it->status.stage = DownloadStatus::Stage::Downloading;
  it->status.model_index = modelIdx;
  it->status.bytes_downloaded = 0;
  publish(it->status);

  const Model& model = it->models[modelIdx];

  ::t3k::net::HttpRequest req;
  req.method = ::t3k::net::HttpMethod::Get;
  req.url    = model.model_url;
  req.headers["Accept"]     = "application/octet-stream";
  req.headers["User-Agent"] = "TONE3000Player/0.1";
  // model_url is server-issued (often a pre-signed CDN link) and
  // Bearer-gated on tone3000.com. Re-attach the Bearer here so the
  // GET resolves; some CDNs accept the token, others ignore it
  // harmlessly.
  if (auto tok = Session::instance().accessTokenIfValid(); tok.has_value()) {
    req.headers["Authorization"] = "Bearer " + *tok;
  }
  // Generous timeout for model bodies (5 minutes — typical 50 MB tone
  // completes in <60s on modest broadband).
  req.timeout_ms = 5 * 60 * 1000;

  it->token = ::t3k::net::HttpClient::instance().send(
      std::move(req),
      [this, it, modelIdx](const ::t3k::net::HttpResponse& res) {
        if (res.canceled) {
          this->failItem(it, "canceled");
          return;
        }
        if (res.status_code < 200 || res.status_code >= 300) {
          this->failItem(it,
              std::string("download HTTP ") +
              std::to_string(res.status_code) + " " + res.error_message);
          return;
        }
        if (res.body.empty()) {
          this->failItem(it, "empty response body");
          return;
        }
        this->stepWrite(it, modelIdx, res.body);
      });
}

void Downloader::stepWrite(std::shared_ptr<Item> it, int modelIdx,
                            const std::vector<uint8_t>& bytes)
{
  it->status.stage = DownloadStatus::Stage::Writing;
  it->status.bytes_downloaded = static_cast<int64_t>(bytes.size());
  publish(it->status);

  const Tone&  tone  = it->tone;
  const Model& model = it->models[modelIdx];

  // Resolve target directory: <toneRoot>/<tone_id>/
  const std::string toneRoot = ::t3k::settings::instance().tone3000_root;
  if (toneRoot.empty()) {
    this->failItem(it, "Library folder not configured. Pick one in Settings.");
    return;
  }
  const std::string dir = ::t3k::library::Paths::toneDir(
      toneRoot, std::to_string(tone.id));
  if (dir.empty()) {
    this->failItem(it, "Could not create per-tone directory");
    return;
  }

  // Filename = sanitized model.name + extension-derived-from-platform.
  const std::string sanitized = sanitizeFilename(model.name);
  const std::string ext       = extForPlatform(tone.platform);
  const std::string localPath = dir + sanitized + ext;

  if (!::t3k::library::Paths::atomicWriteFile(
          localPath, bytes.data(), bytes.size())) {
    this->failItem(it, "Disk write failed for " + sanitized + ext);
    return;
  }

  // Sidecar JSON — sibling of the model file. Non-fatal if it fails;
  // the bytes are on disk and LibraryDb will surface the model under
  // its filename even without the sidecar.
  ::t3k::library::ModelMeta meta = buildModelMeta(tone, model, localPath);
  (void)::t3k::library::writeSidecarFor(localPath, meta);

  // Backfill size_bytes from disk so the row matches what was just
  // written. (mtime is left at 0; the next library scan stat()s it.)
  std::error_code ec;
  const auto fp = fs::u8path(localPath);
  meta.size_bytes = static_cast<int64_t>(fs::file_size(fp, ec));
  if (ec) meta.size_bytes = static_cast<int64_t>(bytes.size());

  const int64_t rowId =
      ::t3k::library::LibraryDb::instance().upsertModel(meta);
  if (rowId > 0) {
    ::t3k::library::EventBus::instance().post(
        ::t3k::library::LibraryEvent::ModelAdded, rowId);
  }

  // Recurse onto the next model. When modelIdx hits total_models the
  // base case in stepDownload triggers finish().
  this->stepDownload(it, modelIdx + 1);
}

void Downloader::finish(std::shared_ptr<Item> it)
{
  it->status.stage = DownloadStatus::Stage::Done;
  publish(it->status);
}

void Downloader::failItem(std::shared_ptr<Item> it, std::string msg)
{
  it->status.stage = DownloadStatus::Stage::Failed;
  it->status.error_message = std::move(msg);
  publish(it->status);
}

}  // namespace t3k::cloud
