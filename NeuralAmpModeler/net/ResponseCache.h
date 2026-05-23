// ResponseCache.h — in-memory LRU + on-disk stub for HTTP responses.
//
// Two cache tiers:
//   - In-memory LRU keyed by `URL|sorted-header-kv`. Used implicitly by
//     HttpClient for every GET; entries honor `Cache-Control: max-age=N`
//     (anything else is ignored) or fall back to the constructor's
//     default TTL.
//   - On-disk single-file-per-key under `%LOCALAPPDATA%\TONE3000\
//     cache\img\`. Phase 4 ships a minimal stub: putToDisk / getFromDisk
//     bytes-only, opt-in. Phase 7 thumbnail download wires this for real.
//
// Singleton — HttpClient owns the only useful instance via instance().

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "HttpRequest.h"
#include "HttpResponse.h"

namespace t3k::net {

class ResponseCache {
 public:
  static ResponseCache& instance();

  // Constructor exposed for tests; the singleton uses (200, 5min).
  ResponseCache(std::size_t maxEntries = 200,
                std::chrono::seconds defaultTtl = std::chrono::minutes(5));

  // Look up a cached response. Returns nullopt for non-GET requests,
  // a cold cache, or an expired entry. Touches the LRU on hit.
  std::optional<HttpResponse> get(const HttpRequest& req);

  // Insert (or replace) the response for `req`. Honors
  // `Cache-Control: max-age=N`; otherwise uses mDefaultTtl. Only
  // status_code in [200, 299] are cached.
  void put(const HttpRequest& req, const HttpResponse& res);

  void clear();
  std::size_t size() const;

  // ----- On-disk stub --------------------------------------------------
  //
  // `key` should be a filesystem-safe string — the caller is responsible
  // for hashing or sanitizing if needed. Phase 4 doesn't enforce length
  // limits; Phase 7 will pick a real key strategy.

  bool putToDisk(const std::string& key, const std::vector<uint8_t>& bytes);
  std::optional<std::vector<uint8_t>> getFromDisk(const std::string& key);

 private:
  ResponseCache(const ResponseCache&) = delete;
  ResponseCache& operator=(const ResponseCache&) = delete;

  // Build the canonical cache key. Lowercased URL + sorted (k:v) header
  // pairs joined by '|'.
  std::string keyOf(const HttpRequest& req) const;

  // Evict expired and over-capacity entries. Caller holds mMtx.
  void evictLocked();

  // Parse a Cache-Control header value for `max-age=N`. Returns 0 when
  // not present / not parsable.
  static int parseMaxAge(const std::string& cacheControl);

  struct Entry {
    HttpResponse                          res;
    std::chrono::steady_clock::time_point expiresAt;
    std::list<std::string>::iterator      lruPos;     // points into mLru
  };

  mutable std::mutex                       mMtx;
  std::unordered_map<std::string, Entry>   mEntries;
  std::list<std::string>                   mLru;       // front = MRU
  std::size_t                              mMaxEntries;
  std::chrono::seconds                     mDefaultTtl;
};

}  // namespace t3k::net
