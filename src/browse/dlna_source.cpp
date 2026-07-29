// SPDX-License-Identifier: GPL-3.0-or-later
//
// browse::Source / browse::Provider backed by UPnP/DLNA media servers:
// SSDP discovery on one side, ContentDirectory Browse plus the DIDL ->
// browse::Entry conversion on the other.
#include "browse/dlna_source.h"

#include <algorithm>

#include "upnp/http.h"
#include "upnp/ssdp.h"

namespace browse {

namespace {

std::string HostOf(const std::string& url) {
  upnp::Url u;
  if (!upnp::Url::Parse(url, u))
    return url;
  return u.host;
}

Entry::Kind KindOf(const upnp::DidlObject& o) {
  if (o.container)
    return Entry::Kind::Folder;
  if (o.IsAudio())
    return Entry::Kind::Audio;
  if (o.IsVideo())
    return Entry::Kind::Video;
  if (o.IsImage())
    return Entry::Kind::Image;
  return Entry::Kind::Other;
}

// protocolInfo is "http-get:*:video/x-matroska:DLNA...."; the MIME type in
// the third field is the useful part.
std::string MimeOf(const std::string& protocol_info) {
  size_t a = protocol_info.find(':');
  size_t b = (a == std::string::npos) ? a : protocol_info.find(':', a + 1);
  size_t c = (b == std::string::npos) ? b : protocol_info.find(':', b + 1);
  if (b == std::string::npos)
    return {};
  return protocol_info.substr(b + 1,
    (c == std::string::npos ? protocol_info.size() : c) - b - 1);
}

Entry Convert(const upnp::DidlObject& o) {
  Entry e;
  e.id = o.id;
  e.title = o.title;
  e.kind = KindOf(o);
  e.child_count = o.child_count;
  e.artist = o.artist;
  e.album = o.album;
  e.genre = o.genre;
  e.date = o.date;
  e.art_url = o.ArtUrl();
  e.format = MimeOf(o.protocol_info);
  e.res_url = o.res_url;
  e.duration_us = o.duration_us;
  e.size_bytes = o.size_bytes;
  e.resolution = o.resolution;
  return e;
}

} // namespace

// ---------------------------------------------------------------------------
// DlnaSource
// ---------------------------------------------------------------------------

DlnaSource::DlnaSource(upnp::MediaServer server) : server_(std::move(server)) {
  const std::string host = HostOf(server_.location);
  detail_ = server_.model.empty() ? host : server_.model + "  -  " + host;
}

bool DlnaSource::Browse(const std::string& id, Listing& out,
                        std::string& error) {
  upnp::BrowseResult result;
  if (!upnp::Browse(server_, id, result, error))
    return false;
  out.entries.reserve(result.objects.size());
  for (const upnp::DidlObject& o : result.objects)
    out.entries.push_back(Convert(o));
  return true;
}

// ---------------------------------------------------------------------------
// DlnaProvider
// ---------------------------------------------------------------------------

DlnaProvider::DlnaProvider(int discovery_wait_ms)
  : discovery_wait_ms_(discovery_wait_ms) {}

bool DlnaProvider::Discover(std::vector<SourcePtr>& out, std::string& error) {
  std::vector<upnp::SsdpResult> found =
    upnp::SsdpSearch(discovery_wait_ms_, error);
  if (found.empty() && !error.empty())
    return false;

  std::vector<upnp::MediaServer> servers;
  for (const upnp::SsdpResult& r : found) {
    upnp::MediaServer server;
    std::string derr;
    if (upnp::DescribeServer(r.location, server, derr))
      servers.push_back(std::move(server));
  }

  // The same server can answer on several interfaces with different
  // LOCATIONs but one UDN.
  std::sort(servers.begin(), servers.end(),
    [](const upnp::MediaServer& a, const upnp::MediaServer& b) {
      return a.udn < b.udn;
    });
  servers.erase(std::unique(servers.begin(), servers.end(),
    [](const upnp::MediaServer& a, const upnp::MediaServer& b) {
      return !a.udn.empty() && a.udn == b.udn;
    }), servers.end());
  std::sort(servers.begin(), servers.end(),
    [](const upnp::MediaServer& a, const upnp::MediaServer& b) {
      return a.friendly_name < b.friendly_name;
    });

  for (upnp::MediaServer& s : servers)
    out.push_back(std::make_shared<DlnaSource>(std::move(s)));
  return true;
}

} // namespace browse
