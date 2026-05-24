// Tone3000Client.h — typed wrapper over tone3000.com/api/v1
// (Phase 6).
//
// Composes net::HttpClient (Phase 4) with cloud::Session (Phase 5):
// every request automatically attaches `Authorization: Bearer
// <access_token>` from the current Session if one is valid. The
// catalog endpoints all REQUIRE auth — calling without a session
// returns Result{success=false, error_message="401 …"}.
//
// All methods are fire-and-forget; the completion lambda fires on a
// WorkerPool thread. UI callers must marshal back to the GUI thread
// themselves (typically by stashing the result under a mutex and
// letting the next paint cycle pick it up — same pattern as Phase 5
// Session events).
//
// The Phase 4 ResponseCache is consulted transparently for GETs that
// emit Cache-Control: max-age — we don't manage that here.

#pragma once

#include <functional>
#include <string>

#include "Tone3000Types.h"
#include "../net/CancellationToken.h"

namespace t3k::cloud {

struct ToneSearchResult {
  bool success = false;
  int http_status = 0;
  std::string error_message;
  PaginatedResponse<Tone> data;
};

struct ToneResult {
  bool success = false;
  int http_status = 0;
  std::string error_message;
  Tone data;
};

struct ModelListResult {
  bool success = false;
  int http_status = 0;
  std::string error_message;
  PaginatedResponse<Model> data;
};

class Tone3000Client {
public:
  // Meyers singleton — internal state is just a base-URL string and
  // (optionally) a swap point for tests.
  static Tone3000Client& instance();

  // GET /api/v1/tones/search
  net::CancellationToken searchTones(SearchTonesParams params,
                                     std::function<void(ToneSearchResult)> onDone);

  // GET /api/v1/tones/{id}
  net::CancellationToken getTone(int id,
                                 std::function<void(ToneResult)> onDone);

  // GET /api/v1/models?tone_id={id}
  net::CancellationToken listModels(int tone_id, int page, int page_size,
                                    std::function<void(ModelListResult)> onDone);

  // For tests: override the base URL (default
  // "https://www.tone3000.com/api/v1"). Returns the previous value.
  std::string setBaseUrl(std::string baseUrl);

private:
  Tone3000Client();
  ~Tone3000Client() = default;
  Tone3000Client(const Tone3000Client&) = delete;
  Tone3000Client& operator=(const Tone3000Client&) = delete;

  // Build "?k=v&k=v…" with proper percent-encoding for one search request.
  std::string buildSearchQuery(const SearchTonesParams& p) const;

  std::string mBaseUrl;
};

}  // namespace t3k::cloud
