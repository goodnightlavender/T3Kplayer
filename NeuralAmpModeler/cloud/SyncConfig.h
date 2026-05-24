// SyncConfig.h — build-time constants for the Phase 8 library-sync
// CloudFlare Worker.
//
// kLibrarySyncUrl is the deployed Worker's URL (no trailing slash).
// Until it's a real URL, isConfigured() returns false and
// cloud::LibrarySync is a complete no-op — the plug-in behaves
// identically to Phase 7.
//
// To activate sync:
//   1. cd nam-fork/workers/library-sync
//   2. Follow the README to create a D1 database + deploy the Worker.
//   3. Replace the placeholder below with the URL wrangler prints
//      after `wrangler deploy`.
//   4. Rebuild the plug-in.
//
// Same pattern as Phase 5's OAuthConfig.h (kClientId).

#pragma once

#include <cstring>

namespace t3k::cloud::sync {

// Worker base URL — no trailing slash. Example after deployment:
//   "https://tone3000-library-sync.<your-subdomain>.workers.dev"
constexpr const char* kLibrarySyncUrl = "REPLACE_ME";

inline bool isConfigured()
{
  return kLibrarySyncUrl != nullptr
      && std::strcmp(kLibrarySyncUrl, "REPLACE_ME") != 0
      && std::strlen(kLibrarySyncUrl) > 0;
}

}  // namespace t3k::cloud::sync
