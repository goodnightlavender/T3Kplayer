// ResponseCache.cpp — implementation. See ResponseCache.h.
//
// LRU is a standard hash-map + linked-list combo:
//   - `mEntries[key]`     -> stored value (+ expiry + LRU iterator)
//   - `mLru` (front = MRU) holds the keys; touch-on-hit means erase from
//     wherever the iterator points and re-insert at front.

#include "ResponseCache.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <utility>

#include "Logging.h"
#include "../library/Paths.h"

namespace t3k::net {

namespace {

std::string AsciiLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return s;
}

}  // namespace

ResponseCache& ResponseCache::instance()
{
  static ResponseCache c;
  return c;
}

ResponseCache::ResponseCache(std::size_t maxEntries,
                             std::chrono::seconds defaultTtl)
: mMaxEntries(maxEntries == 0 ? 1 : maxEntries)
, mDefaultTtl(defaultTtl.count() > 0 ? defaultTtl : std::chrono::seconds{60})
{
}

std::string ResponseCache::keyOf(const HttpRequest& req) const
{
  // Lowercase the URL for case-insensitive scheme/host matching;
  // technically the path is case-sensitive but in practice TONE3000
  // endpoints don't rely on case. Sort headers so order doesn't
  // disrupt key stability.
  std::string out = AsciiLower(req.url);
  out.reserve(out.size() + req.headers.size() * 32);

  std::map<std::string, std::string> sorted(req.headers.begin(),
                                            req.headers.end());
  for (const auto& [k, v] : sorted) {
    out.push_back('|');
    out += AsciiLower(k);
    out.push_back(':');
    out += v;
  }
  return out;
}

int ResponseCache::parseMaxAge(const std::string& cacheControl)
{
  if (cacheControl.empty()) return 0;
  const std::string lc = AsciiLower(cacheControl);
  const std::string marker = "max-age=";
  const std::size_t pos = lc.find(marker);
  if (pos == std::string::npos) return 0;

  std::size_t cur = pos + marker.size();
  std::string digits;
  while (cur < lc.size() &&
         std::isdigit(static_cast<unsigned char>(lc[cur]))) {
    digits.push_back(lc[cur++]);
  }
  if (digits.empty()) return 0;
  try {
    return std::stoi(digits);
  } catch (...) {
    return 0;
  }
}

void ResponseCache::evictLocked()
{
  const auto now = std::chrono::steady_clock::now();

  // Drop expired entries — walk the map and erase in-place.
  // The LRU list itself stays in sync via the per-entry iterator.
  for (auto it = mEntries.begin(); it != mEntries.end(); ) {
    if (it->second.expiresAt <= now) {
      mLru.erase(it->second.lruPos);
      it = mEntries.erase(it);
    } else {
      ++it;
    }
  }

  // Drop oldest until we fit in capacity.
  while (mEntries.size() > mMaxEntries && !mLru.empty()) {
    const std::string& evictKey = mLru.back();
    mEntries.erase(evictKey);
    mLru.pop_back();
  }
}

std::optional<HttpResponse> ResponseCache::get(const HttpRequest& req)
{
  if (req.method != HttpMethod::Get) return std::nullopt;

  std::lock_guard<std::mutex> lk(mMtx);
  evictLocked();

  const std::string k = keyOf(req);
  auto it = mEntries.find(k);
  if (it == mEntries.end()) return std::nullopt;

  if (it->second.expiresAt <= std::chrono::steady_clock::now()) {
    // Already dropped by evictLocked; defensive return.
    return std::nullopt;
  }

  // Touch LRU — move this key to front.
  mLru.erase(it->second.lruPos);
  mLru.push_front(k);
  it->second.lruPos = mLru.begin();

  return it->second.res;
}

void ResponseCache::put(const HttpRequest& req, const HttpResponse& res)
{
  if (req.method != HttpMethod::Get) return;
  if (res.status_code < 200 || res.status_code >= 300) return;

  // Honor Cache-Control: max-age, else default.
  std::chrono::seconds ttl = mDefaultTtl;
  auto ccIt = res.headers.find("cache-control");
  if (ccIt != res.headers.end()) {
    const int maxAge = parseMaxAge(ccIt->second);
    if (maxAge > 0) ttl = std::chrono::seconds(maxAge);
  }

  std::lock_guard<std::mutex> lk(mMtx);
  const std::string k = keyOf(req);

  // Replace existing if present.
  auto it = mEntries.find(k);
  if (it != mEntries.end()) {
    mLru.erase(it->second.lruPos);
    mEntries.erase(it);
  }

  mLru.push_front(k);
  Entry e;
  e.res       = res;
  e.expiresAt = std::chrono::steady_clock::now() + ttl;
  e.lruPos    = mLru.begin();
  mEntries.emplace(k, std::move(e));

  evictLocked();
}

void ResponseCache::clear()
{
  std::lock_guard<std::mutex> lk(mMtx);
  mEntries.clear();
  mLru.clear();
}

std::size_t ResponseCache::size() const
{
  std::lock_guard<std::mutex> lk(mMtx);
  return mEntries.size();
}

// ----- On-disk stub ------------------------------------------------------

bool ResponseCache::putToDisk(const std::string& key,
                              const std::vector<uint8_t>& bytes)
{
  if (key.empty()) return false;
  ::t3k::library::Paths::ensureAppDataLayout();
  const std::string dir = ::t3k::library::Paths::thumbnailCacheDir();
  if (dir.empty()) return false;

  const std::string path = dir + key;
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    T3K_NET_LOG("warn", "putToDisk: open failed for %s", path.c_str());
    return false;
  }
  if (!bytes.empty()) {
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  }
  return static_cast<bool>(f);
}

std::optional<std::vector<uint8_t>> ResponseCache::getFromDisk(
    const std::string& key)
{
  if (key.empty()) return std::nullopt;
  const std::string dir = ::t3k::library::Paths::thumbnailCacheDir();
  if (dir.empty()) return std::nullopt;

  const std::string path = dir + key;
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return std::nullopt;
  const std::streamsize n = f.tellg();
  if (n <= 0) return std::vector<uint8_t>{};
  f.seekg(0, std::ios::beg);
  std::vector<uint8_t> out(static_cast<std::size_t>(n));
  if (!f.read(reinterpret_cast<char*>(out.data()), n)) return std::nullopt;
  return out;
}

}  // namespace t3k::net
