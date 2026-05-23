// SessionEvent.h — tagged-union event published by cloud::Session
// whenever the auth state machine transitions (Phase 5).
//
// Subscribers (currently just ui::ToneRoot) get one event per state
// transition; they swap the header pill/avatar accordingly. The event
// carries the User payload for SignedIn (so the UI doesn't have to
// re-query Session under a lock) and an error_message for
// SignInFailed.

#pragma once

#include <optional>
#include <string>

#include "User.h"

namespace t3k::cloud {

struct SessionEvent {
  enum class Kind {
    SignInStarted,
    SignedIn,
    SignedOut,
    SessionExpired,
    SignInFailed,
  };

  Kind kind;
  std::optional<User> user;
  std::string error_message;
};

}  // namespace t3k::cloud
