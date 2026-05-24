// ThumbnailCache.cpp — see ThumbnailCache.h.

#include "ThumbnailCache.h"

#include "Crypto.h"
#include "../library/Paths.h"
#include "../net/HttpClient.h"
#include "../net/HttpRequest.h"
#include "../net/HttpResponse.h"

#include <filesystem>
#include <mutex>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace t3k::cloud {

namespace {

namespace fs = std::filesystem;

// Hex-encode the first 20 bytes of a SHA-256 — gives a 40-char file
// name, plenty of collision resistance for an image cache.
std::string sha256HexPrefix(const std::string& s)
{
  const auto h = sha256(s);
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(40);
  for (int i = 0; i < 20; ++i) {
    out.push_back(kHex[(h[i] >> 4) & 0xF]);
    out.push_back(kHex[h[i] & 0xF]);
  }
  return out;
}

// Process-wide in-flight tracker: url → callbacks waiting on it.
struct State {
  std::mutex mtx;
  std::unordered_map<std::string,
                     std::vector<ThumbnailCache::OnReady>> inFlight;
};

State& state()
{
  static State s;
  return s;
}

}  // namespace

ThumbnailCache& ThumbnailCache::instance()
{
  static ThumbnailCache inst;
  return inst;
}

std::string ThumbnailCache::pathForUrl(const std::string& url) const
{
  const std::string dir = ::t3k::library::Paths::thumbnailCacheDir();
  if (dir.empty() || url.empty()) return {};
  return dir + sha256HexPrefix(url) + ".jpg";
}

void ThumbnailCache::fetch(const std::string& url, OnReady cb)
{
  if (url.empty()) {
    if (cb) cb("", false);
    return;
  }

  const std::string localPath = pathForUrl(url);
  if (localPath.empty()) {
    if (cb) cb("", false);
    return;
  }

  // ── Cache hit? ────────────────────────────────────────────────
  std::error_code ec;
  if (fs::exists(fs::u8path(localPath), ec) && !ec) {
    if (cb) cb(localPath, true);
    return;
  }

  // ── Already in flight for this URL? Append the callback and bail.
  {
    std::lock_guard<std::mutex> lk(state().mtx);
    auto it = state().inFlight.find(url);
    if (it != state().inFlight.end()) {
      it->second.push_back(std::move(cb));
      return;
    }
    // First requester — register the in-flight slot before kicking
    // the HTTP fetch so a duplicate request that races us will see
    // the entry and queue behind.
    state().inFlight[url].push_back(std::move(cb));
  }

  // ── Cold fetch: GET → atomic write → resolve all queued callbacks.
  // Ensure the cache directory exists; first user on a fresh install
  // hits this path.
  ::t3k::library::Paths::ensureAppDataLayout();

  ::t3k::net::HttpRequest req;
  req.method = ::t3k::net::HttpMethod::Get;
  req.url    = url;
  // Most CDN thumbnails are public; no Authorization header. We still
  // accept any image-typed response.
  req.headers["Accept"]     = "image/*";
  req.headers["User-Agent"] = "TONE3000Player/0.1";

  ::t3k::net::HttpClient::instance().send(std::move(req),
      [url, localPath](const ::t3k::net::HttpResponse& res) {
        const bool ok =
            (res.status_code >= 200 && res.status_code < 300) &&
            !res.body.empty();

        bool wroteOk = false;
        if (ok) {
          wroteOk = ::t3k::library::Paths::atomicWriteFile(
              localPath,
              res.body.data(),
              res.body.size());
        }
        const bool finalOk = ok && wroteOk;

        // Drain the in-flight queue for this URL and notify each.
        std::vector<OnReady> waiters;
        {
          std::lock_guard<std::mutex> lk(state().mtx);
          auto it = state().inFlight.find(url);
          if (it != state().inFlight.end()) {
            waiters = std::move(it->second);
            state().inFlight.erase(it);
          }
        }
        for (auto& w : waiters) {
          if (w) w(finalOk ? localPath : std::string{}, finalOk);
        }
      });
}

}  // namespace t3k::cloud
