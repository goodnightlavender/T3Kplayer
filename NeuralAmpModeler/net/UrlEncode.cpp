// UrlEncode.cpp — implementation. See UrlEncode.h.

#include "UrlEncode.h"

#include <cstdio>

namespace t3k::net {

namespace {

inline bool IsUnreserved(unsigned char c)
{
  // RFC 3986 §2.3 unreserved = ALPHA / DIGIT / "-" / "." / "_" / "~"
  return (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') ||
         c == '-' || c == '_' || c == '.' || c == '~';
}

}  // namespace

std::string urlEncode(const std::string& s)
{
  std::string out;
  out.reserve(s.size() * 3 / 2);          // amortize for sparse encoding
  char hex[4];
  for (unsigned char c : s) {
    if (IsUnreserved(c)) {
      out.push_back(static_cast<char>(c));
    } else {
      std::snprintf(hex, sizeof(hex), "%%%02X", c);
      out.append(hex);
    }
  }
  return out;
}

std::string urlEncodeQueryString(const std::map<std::string, std::string>& kv)
{
  std::string out;
  bool first = true;
  for (const auto& [k, v] : kv) {
    if (!first) out.push_back('&');
    out += urlEncode(k);
    out.push_back('=');
    out += urlEncode(v);
    first = false;
  }
  return out;
}

}  // namespace t3k::net
