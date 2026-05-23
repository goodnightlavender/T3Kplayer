// User.h — POD describing the signed-in TONE3000 user (Phase 5).
//
// Populated by Session::attemptRefresh or the mock-sign-in path. For
// Phase 5, the real OAuth flow leaves most fields empty (a separate
// /api/v1/user fetch comes in Phase 6). The mock path fills all
// fields with synthetic test values so the UI surface can be exercised
// end-to-end.

#pragma once

#include <string>

namespace t3k::cloud {

struct User {
  std::string id;
  std::string username;
  std::string display_name;
  std::string email;
  std::string avatar_url;
};

}  // namespace t3k::cloud
