// HttpResponse.h — POD describing one inbound HTTP response.
//
// Populated by the WinHTTP worker thread and handed to the caller's
// completion lambda. `status_code == 0` signals a transport-level
// failure (DNS, TLS, connect, etc.) — `error_message` carries detail.
// `canceled == true` means CancellationToken::cancel() fired before
// the response was fully received.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace t3k::net {

struct HttpResponse {
  int status_code = 0;                    // 0 = couldn't connect (see error_message)
  std::map<std::string, std::string> headers;  // keys lower-cased on insert
  std::vector<uint8_t> body;
  int64_t elapsed_ms = 0;
  std::string error_message;              // populated when status_code == 0 OR on cancel
  bool canceled = false;
};

}  // namespace t3k::net
