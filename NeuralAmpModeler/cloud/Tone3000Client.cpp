// Tone3000Client.cpp — see Tone3000Client.h.

#include "Tone3000Client.h"

#include "Session.h"
#include "../net/HttpClient.h"
#include "../net/HttpRequest.h"
#include "../net/HttpResponse.h"
#include "../net/UrlEncode.h"

// JSON parser: already vendored via iPlug2's Dependencies/Extras
// (used by Phase 5 OAuth and Phase 3 sidecar JSON). Path is set up
// in the .vcxproj include dirs.
#include "nlohmann/json.hpp"

#include <algorithm>
#include <sstream>

namespace t3k::cloud {

namespace {

using json = nlohmann::json;

// Pull a string field with a default — TONE3000's nullable fields
// come back as JSON null which would throw on operator string().
std::string s(const json& j, const char* key, const char* dflt = "") {
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) return dflt;
  if (it->is_string()) return it->get<std::string>();
  // Numbers/bools occasionally appear where the SDK declares string —
  // be defensive about minor schema drift.
  return it->dump();
}

std::optional<std::string> sOpt(const json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) return std::nullopt;
  if (it->is_string()) return it->get<std::string>();
  return it->dump();
}

int i(const json& j, const char* key, int dflt = 0) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) return dflt;
  if (it->is_number_integer()) return it->get<int>();
  if (it->is_number_unsigned()) return static_cast<int>(it->get<unsigned>());
  if (it->is_number_float())   return static_cast<int>(it->get<double>());
  return dflt;
}

std::optional<bool> bOpt(const json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) return std::nullopt;
  if (it->is_boolean()) return it->get<bool>();
  return std::nullopt;
}

EmbeddedUser parseEmbeddedUser(const json& j) {
  EmbeddedUser u;
  u.id         = s(j, "id");
  u.username   = s(j, "username");
  u.avatar_url = sOpt(j, "avatar_url");
  u.url        = s(j, "url");
  return u;
}

Make parseMake(const json& j) {
  Make m;
  auto it = j.find("id");
  if (it != j.end() && it->is_number_integer()) m.id = it->get<int>();
  m.name = s(j, "name");
  return m;
}

Tag parseTag(const json& j) {
  Tag t;
  auto it = j.find("id");
  if (it != j.end() && it->is_number_integer()) t.id = it->get<int>();
  t.name = s(j, "name");
  return t;
}

Tone parseTone(const json& j) {
  Tone t;
  t.id          = i(j, "id");
  t.user_id     = s(j, "user_id");
  if (auto u = j.find("user"); u != j.end() && u->is_object()) {
    t.user = parseEmbeddedUser(*u);
  }
  t.created_at  = sOpt(j, "created_at");
  t.updated_at  = sOpt(j, "updated_at");
  t.title       = s(j, "title");
  t.description = sOpt(j, "description");
  if (auto g = j.find("gear"); g != j.end() && g->is_string()) {
    if (auto e = gearFromWire(g->get<std::string>()); e.has_value()) t.gear = *e;
  }
  if (auto imgs = j.find("images"); imgs != j.end() && imgs->is_array()) {
    std::vector<std::string> v;
    for (const auto& x : *imgs) if (x.is_string()) v.push_back(x.get<std::string>());
    t.images = std::move(v);
  }
  t.is_public = bOpt(j, "is_public");
  if (auto lnks = j.find("links"); lnks != j.end() && lnks->is_array()) {
    std::vector<std::string> v;
    for (const auto& x : *lnks) if (x.is_string()) v.push_back(x.get<std::string>());
    t.links = std::move(v);
  }
  if (auto p = j.find("platform"); p != j.end() && p->is_string()) {
    if (auto e = platformFromWire(p->get<std::string>()); e.has_value()) t.platform = *e;
  }
  if (auto lic = j.find("license"); lic != j.end() && lic->is_string()) {
    t.license = licenseFromWire(lic->get<std::string>());
  }
  if (auto sz = j.find("sizes"); sz != j.end() && sz->is_array()) {
    for (const auto& x : *sz) {
      if (x.is_string()) {
        if (auto e = sizeFromWire(x.get<std::string>()); e.has_value())
          t.sizes.push_back(*e);
      }
    }
  }
  if (auto mks = j.find("makes"); mks != j.end() && mks->is_array()) {
    for (const auto& x : *mks) if (x.is_object()) t.makes.push_back(parseMake(x));
  }
  if (auto tgs = j.find("tags"); tgs != j.end() && tgs->is_array()) {
    for (const auto& x : *tgs) if (x.is_object()) t.tags.push_back(parseTag(x));
  }
  t.models_count    = i(j, "models_count");
  t.downloads_count = i(j, "downloads_count");
  t.favorites_count = i(j, "favorites_count");
  t.url             = s(j, "url");
  return t;
}

Model parseModel(const json& j) {
  Model m;
  m.id         = i(j, "id");
  m.created_at = s(j, "created_at");
  m.updated_at = s(j, "updated_at");
  m.user_id    = s(j, "user_id");
  m.model_url  = s(j, "model_url");
  m.name       = s(j, "name");
  if (auto sz = j.find("size"); sz != j.end() && sz->is_string()) {
    if (auto e = sizeFromWire(sz->get<std::string>()); e.has_value()) m.size = *e;
  }
  m.tone_id    = i(j, "tone_id");
  return m;
}

template <class T>
PaginatedResponse<T> parsePage(const json& j, T (*parseOne)(const json&)) {
  PaginatedResponse<T> p;
  p.page        = i(j, "page", 1);
  p.page_size   = i(j, "page_size", 0);
  p.total       = i(j, "total", 0);
  p.total_pages = i(j, "total_pages", 0);
  if (auto d = j.find("data"); d != j.end() && d->is_array()) {
    p.data.reserve(d->size());
    for (const auto& x : *d) if (x.is_object()) p.data.push_back(parseOne(x));
  }
  return p;
}

// Build common headers (Accept + optional Bearer).
std::map<std::string, std::string> baseHeaders() {
  std::map<std::string, std::string> h;
  h["Accept"]       = "application/json";
  h["User-Agent"]   = "TONE3000Player/0.1";
  if (auto tok = Session::instance().accessTokenIfValid(); tok.has_value()) {
    h["Authorization"] = "Bearer " + *tok;
  }
  return h;
}

// Convert an HttpResponse into a {http_status, error_message} pair
// suitable for any of our Result structs. Returns true iff the
// response is a 2xx that the caller should parse as JSON.
bool extractOk(const net::HttpResponse& res, int& outStatus, std::string& outErr) {
  outStatus = res.status_code;
  if (res.canceled) {
    outErr = "canceled";
    return false;
  }
  if (res.status_code == 0) {
    outErr = "transport: " + res.error_message;
    return false;
  }
  if (res.status_code < 200 || res.status_code >= 300) {
    std::ostringstream os;
    os << res.status_code;
    outErr = os.str() + " " + res.error_message;
    if (!res.body.empty()) {
      // Surface a snippet of the response body to make 4xx debugging easier.
      outErr += " ";
      outErr.append(reinterpret_cast<const char*>(res.body.data()),
                    std::min<std::size_t>(res.body.size(), 256));
    }
    return false;
  }
  return true;
}

}  // namespace

Tone3000Client& Tone3000Client::instance() {
  static Tone3000Client inst;
  return inst;
}

Tone3000Client::Tone3000Client()
: mBaseUrl("https://www.tone3000.com/api/v1") {}

std::string Tone3000Client::setBaseUrl(std::string baseUrl) {
  std::string prev = std::move(mBaseUrl);
  mBaseUrl = std::move(baseUrl);
  return prev;
}

std::string Tone3000Client::buildSearchQuery(const SearchTonesParams& p) const {
  // The API expects `gears` and `sizes` as underscore-joined strings
  // (per SDK source: params.gears.join('_')). That breaks the
  // standard "k=v&k=v" encoding only if a value contains a '_' on
  // its own — none of the enums do, so this is safe to build via
  // urlEncodeQueryString.
  std::map<std::string, std::string> kv;
  if (!p.query.empty())  kv["query"]     = p.query;
  if (p.page > 1)        kv["page"]      = std::to_string(p.page);
  if (p.page_size > 0)   kv["page_size"] = std::to_string(p.page_size);
  kv["sort"] = toWire(p.sort);
  if (!p.gears.empty()) {
    std::string joined;
    for (std::size_t k = 0; k < p.gears.size(); ++k) {
      if (k) joined += "_";
      joined += toWire(p.gears[k]);
    }
    kv["gears"] = joined;
  }
  if (!p.sizes.empty()) {
    std::string joined;
    for (std::size_t k = 0; k < p.sizes.size(); ++k) {
      if (k) joined += "_";
      joined += toWire(p.sizes[k]);
    }
    kv["sizes"] = joined;
  }
  return net::urlEncodeQueryString(kv);
}

net::CancellationToken Tone3000Client::searchTones(
    SearchTonesParams params, std::function<void(ToneSearchResult)> onDone)
{
  net::HttpRequest req;
  req.method  = net::HttpMethod::Get;
  req.url     = mBaseUrl + "/tones/search?" + buildSearchQuery(params);
  req.headers = baseHeaders();

  return net::HttpClient::instance().send(std::move(req),
      [onDone = std::move(onDone)](const net::HttpResponse& res) {
        ToneSearchResult r;
        if (!extractOk(res, r.http_status, r.error_message)) {
          if (onDone) onDone(std::move(r));
          return;
        }
        try {
          const std::string bodyStr(res.body.begin(), res.body.end());
          const json j = json::parse(bodyStr);
          r.data = parsePage<Tone>(j, parseTone);
          r.success = true;
        } catch (const std::exception& e) {
          r.error_message = std::string("json parse: ") + e.what();
        }
        if (onDone) onDone(std::move(r));
      });
}

net::CancellationToken Tone3000Client::getTone(
    int id, std::function<void(ToneResult)> onDone)
{
  net::HttpRequest req;
  req.method  = net::HttpMethod::Get;
  req.url     = mBaseUrl + "/tones/" + std::to_string(id);
  req.headers = baseHeaders();

  return net::HttpClient::instance().send(std::move(req),
      [onDone = std::move(onDone)](const net::HttpResponse& res) {
        ToneResult r;
        if (!extractOk(res, r.http_status, r.error_message)) {
          if (onDone) onDone(std::move(r));
          return;
        }
        try {
          const std::string bodyStr(res.body.begin(), res.body.end());
          const json j = json::parse(bodyStr);
          r.data = parseTone(j);
          r.success = true;
        } catch (const std::exception& e) {
          r.error_message = std::string("json parse: ") + e.what();
        }
        if (onDone) onDone(std::move(r));
      });
}

net::CancellationToken Tone3000Client::listModels(
    int tone_id, int page, int page_size,
    std::function<void(ModelListResult)> onDone)
{
  std::map<std::string, std::string> kv;
  kv["tone_id"]   = std::to_string(tone_id);
  if (page > 0)      kv["page"]      = std::to_string(page);
  if (page_size > 0) kv["page_size"] = std::to_string(page_size);

  net::HttpRequest req;
  req.method  = net::HttpMethod::Get;
  req.url     = mBaseUrl + "/models?" + net::urlEncodeQueryString(kv);
  req.headers = baseHeaders();

  return net::HttpClient::instance().send(std::move(req),
      [onDone = std::move(onDone)](const net::HttpResponse& res) {
        ModelListResult r;
        if (!extractOk(res, r.http_status, r.error_message)) {
          if (onDone) onDone(std::move(r));
          return;
        }
        try {
          const std::string bodyStr(res.body.begin(), res.body.end());
          const json j = json::parse(bodyStr);
          r.data = parsePage<Model>(j, parseModel);
          r.success = true;
        } catch (const std::exception& e) {
          r.error_message = std::string("json parse: ") + e.what();
        }
        if (onDone) onDone(std::move(r));
      });
}

}  // namespace t3k::cloud
