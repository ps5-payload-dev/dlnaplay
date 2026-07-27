// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal blocking HTTP/1.1 client, just enough for UPnP: fetching device
// description documents and posting SOAP envelopes to control URLs. Media
// itself is NOT transferred through this; playback hands the media URL
// straight to libavformat, which has its own (much better) HTTP stack.
#ifndef UPNP_HTTP_H
#define UPNP_HTTP_H

#include <cstdint>
#include <map>
#include <string>

namespace upnp {

// Broken-down absolute http:// URL.
struct Url {
  std::string host;
  uint16_t port = 80;
  std::string path = "/"; // includes the query string, if any

  // Parses "http://host[:port][/path]". Returns false for anything else
  // (https is not supported; UPnP control endpoints are plain http).
  static bool Parse(const std::string& url, Url& out);
};

// Resolves 'ref' (absolute URL, absolute path, or relative path) against
// 'base', per the subset of RFC 3986 that device descriptions actually use.
std::string ResolveUrl(const std::string& base, const std::string& ref);

struct HttpResponse {
  int status = 0;
  std::map<std::string, std::string> headers; // keys lower-cased
  std::string body;
};

// One-shot request; opens a connection, sends, reads the full response,
// closes. 'extra_headers' lines must be complete ("SOAPACTION: \"...\"") and
// are sent verbatim. Returns false on connect/transport errors, with a
// human-readable reason in 'error'. HTTP error statuses (4xx/5xx) return
// true; the caller inspects response.status.
bool HttpRequest(const std::string& method, const std::string& url,
                 const std::string& extra_headers, const std::string& body,
                 HttpResponse& response, std::string& error,
                 int timeout_ms = 8000);

inline bool HttpGet(const std::string& url, HttpResponse& r, std::string& e) {
  return HttpRequest("GET", url, {}, {}, r, e);
}

} // namespace upnp

#endif
