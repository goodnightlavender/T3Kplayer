// HttpRequest.h — POD describing one outbound HTTP request.
//
// Plain data the worker thread reads while issuing the request via
// WinHTTP. Constructed on the GUI thread (or wherever the request
// originates) and moved into the job lambda submitted to HttpClient.
//
// Phase 4 ships GET/POST/PUT/DELETE. The header map is case-insensitive
// from WinHTTP's point of view but stored verbatim — case folding for
// cache-key normalization happens inside ResponseCache.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace t3k::net {

enum class HttpMethod { Get, Post, Put, Delete };

struct HttpRequest {
  HttpMethod  method = HttpMethod::Get;
  std::string url;                        // "https://api.example.com/v1/x?y=1"
  std::map<std::string, std::string> headers;
  std::vector<uint8_t> body;              // empty for GET; UTF-8 JSON or bytes for POST/PUT
  int timeout_ms = 15'000;                // total budget; split four ways across WinHTTP phases
};

}  // namespace t3k::net
