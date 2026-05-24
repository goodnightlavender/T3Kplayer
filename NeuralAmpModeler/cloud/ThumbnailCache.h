// ThumbnailCache.h — disk-cached image fetcher for Cloud-tab card
// thumbnails (Phase 7).
//
// Given an image URL, the cache:
//   1. Hashes the URL → SHA-256 hex prefix → local file path under
//      %LOCALAPPDATA%\TONE3000\cache\img\<hex>.jpg.
//   2. If the file already exists on disk, fires the caller's
//      OnReady callback SYNCHRONOUSLY with the cached path.
//   3. Otherwise schedules an HTTP GET via net::HttpClient and
//      writes the response body to the cache path atomically; the
//      callback fires on the HTTP worker thread.
//
// Two cards requesting the same URL share one HTTP fetch — the
// second caller's callback is appended to an in-flight queue.
//
// The cache is a Meyers singleton; callers never construct one
// directly.

#pragma once

#include <functional>
#include <string>

namespace t3k::cloud {

class ThumbnailCache {
public:
  static ThumbnailCache& instance();

  // (localPath, ok). `localPath` is the absolute path to the cached
  // file when ok==true, empty when ok==false. Fires on the HTTP
  // worker thread for cold fetches; fires synchronously on the
  // caller's thread for cache hits.
  using OnReady = std::function<void(const std::string& localPath, bool ok)>;

  // No-op for empty URL. Otherwise:
  //   - synchronous OnReady(path, true)  on cache hit
  //   - asynchronous OnReady(path, true) on successful cold fetch
  //   - asynchronous OnReady("", false)  on transport / disk failure
  void fetch(const std::string& url, OnReady cb);

private:
  ThumbnailCache() = default;
  ~ThumbnailCache() = default;
  ThumbnailCache(const ThumbnailCache&) = delete;
  ThumbnailCache& operator=(const ThumbnailCache&) = delete;

  // Compute the cache file path for a URL. Returns empty when the
  // cache directory isn't resolvable.
  std::string pathForUrl(const std::string& url) const;
};

}  // namespace t3k::cloud
