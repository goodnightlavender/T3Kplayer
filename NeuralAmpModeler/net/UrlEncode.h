// UrlEncode.h — RFC 3986 percent-encoder helpers.
//
// Used to escape query parameter values and arbitrary path segments
// before they're stitched into a URL. The unreserved set per RFC 3986
// §2.3 is `A-Za-z0-9-._~` — everything else gets %HH-encoded.

#pragma once

#include <map>
#include <string>

namespace t3k::net {

// Percent-encode `s` so it can be safely embedded in a URL component.
std::string urlEncode(const std::string& s);

// Build a `k=v&k=v` query string from `kv`, with both keys and values
// individually url-encoded. Returns an empty string when `kv` is empty.
std::string urlEncodeQueryString(const std::map<std::string, std::string>& kv);

}  // namespace t3k::net
