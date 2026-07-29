// SPDX-License-Identifier: GPL-3.0-or-later
//
// The servers view (SSDP discovery results) and the browse view (the
// ContentDirectory tree of the chosen server).
#include <algorithm>

#include "app.h"
#include "app_internal.h"
#include "upnp/http.h"
#include "upnp/ssdp.h"

using namespace appdetail;

namespace {

// A short glyph for the entry list; the emoji font is loaded as a fallback
// face, so these render everywhere.
Rml::String IconFor(const upnp::DidlObject& o) {
  if (o.container)
    return "📁";
  if (o.IsAudio())
    return "🎵";
  if (o.IsVideo())
    return "🎬";
  if (o.IsImage())
    return "🖼";
  return "📄";
}

std::string HostOf(const std::string& url) {
  upnp::Url u;
  if (!upnp::Url::Parse(url, u))
    return url;
  return u.host;
}

} // namespace

// ---------------------------------------------------------------------------
// Servers view
// ---------------------------------------------------------------------------
#include <iostream>
void App::StartDiscovery() {
  bind_status_ = "Searching for media servers...";
  model_.DirtyVariable("status");

  busy_ops_++;
  PostTask([this] {
    std::string error;
    std::vector<upnp::SsdpResult> found = upnp::SsdpSearch(kDiscoveryWaitMs, error);

    std::vector<upnp::MediaServer> servers;
    for (const upnp::SsdpResult& r : found) {
      upnp::MediaServer server;
      std::string derr;

      if (upnp::DescribeServer(r.location, server, derr)) {
        servers.push_back(std::move(server));
      }
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

    {
      std::lock_guard<std::mutex> lock(pending_.mutex);
      pending_.servers_ready = true;
      pending_.servers = std::move(servers);
      pending_.discover_error = error;
    }
    busy_ops_--;
  });
}

void App::RebuildServerRows() {
  server_rows_.clear();
  for (const upnp::MediaServer& s : servers_) {
    ServerRow row;
    row.name = s.friendly_name;
    row.detail = s.model.empty() ? HostOf(s.location)
                                 : s.model + "  -  " + HostOf(s.location);
    server_rows_.push_back(std::move(row));
  }
  server_count_ = (int)servers_.size();
  sel_server_ = std::clamp(sel_server_, 0, std::max(0, server_count_ - 1));
  model_.DirtyVariable("servers");
  model_.DirtyVariable("server_count");
  model_.DirtyVariable("sel_server");
  scroll_servers_pending_ = true;
}

void App::HandleKeyServers(Rml::Event& event, int key) {
  switch (key) {
  case Rml::Input::KI_UP:
    sel_server_ = std::max(0, sel_server_ - 1);
    break;
  case Rml::Input::KI_DOWN:
    sel_server_ = std::min(std::max(0, server_count_ - 1), sel_server_ + 1);
    break;
  case Rml::Input::KI_SPACE:
    StartDiscovery();
    event.StopPropagation();
    return;
  case Rml::Input::KI_RETURN:
  case Rml::Input::KI_NUMPADENTER:
    if (sel_server_ < (int)servers_.size())
      OpenServer(sel_server_);
    event.StopPropagation();
    return;
  default:
    return;
  }
  model_.DirtyVariable("sel_server");
  scroll_servers_pending_ = true;
  event.StopPropagation();
}

// ---------------------------------------------------------------------------
// Browse view
// ---------------------------------------------------------------------------

App::BrowseLevel* App::CurrentLevel() {
  return path_.empty() ? nullptr : &path_.back();
}

const upnp::DidlObject* App::SelectedObject() const {
  if (path_.empty())
    return nullptr;
  const BrowseLevel& level = path_.back();
  if (sel_entry_ < 0 || sel_entry_ >= (int)level.objects.size())
    return nullptr;
  return &level.objects[sel_entry_];
}

void App::OpenServer(int index) {
  if (index < 0 || index >= (int)servers_.size())
    return;
  current_server_ = index;
  path_.clear();
  sel_entry_ = 0;

  bind_server_name_ = servers_[index].friendly_name;
  model_.DirtyVariable("server_name");

  view_ = View::Browse;
  bind_view_ = "browse";
  model_.DirtyVariable("view");

  RebuildEntryRows();
  RebuildCrumb();
  RebuildDetail();
  RequestBrowse("0", servers_[index].friendly_name);
}

void App::RequestBrowse(const std::string& object_id, const std::string& title) {
  if (current_server_ < 0)
    return;

  const uint32_t request = ++browse_request_;
  const upnp::MediaServer server = servers_[current_server_];

  busy_ops_++;
  PostTask([this, request, server, object_id, title] {
    upnp::BrowseResult result;
    std::string error;
    upnp::Browse(server, object_id, result, error);
    {
      std::lock_guard<std::mutex> lock(pending_.mutex);
      pending_.browse_ready = true;
      pending_.browse_request = request;
      pending_.browse_object_id = object_id;
      pending_.browse_title = title;
      pending_.browse = std::move(result);
      pending_.browse_error = error;
    }
    busy_ops_--;
  });
}

void App::EnterContainer(const upnp::DidlObject& obj) {
  if (BrowseLevel* level = CurrentLevel())
    level->selection = sel_entry_;
  RequestBrowse(obj.id, obj.title);
}

void App::LeaveContainer() {
  browse_request_++; // invalidate any browse that is still in flight
  if (path_.size() <= 1) {
    // At the root: back to the server list.
    path_.clear();
    current_server_ = -1;
    view_ = View::Servers;
    bind_view_ = "servers";
    model_.DirtyVariable("view");
    scroll_servers_pending_ = true;
    return;
  }
  path_.pop_back();
  sel_entry_ = path_.back().selection;
  RebuildEntryRows();
  RebuildCrumb();
  RebuildDetail();
  scroll_entries_pending_ = true;
}

void App::RebuildEntryRows() {
  entry_rows_.clear();
  if (const BrowseLevel* level = CurrentLevel()) {
    for (const upnp::DidlObject& o : level->objects) {
      EntryRow row;
      row.icon = IconFor(o);
      row.title = o.title.empty() ? "(untitled)" : o.title;
      row.folder = o.container;
      if (o.container) {
        if (o.child_count >= 0)
          row.meta = std::to_string(o.child_count) +
            (o.child_count == 1 ? " entry" : " entries");
      } else {
        std::string meta;
        if (o.duration_us > 0)
          meta = FormatTime(o.duration_us);
        if (!o.resolution.empty())
          meta += (meta.empty() ? "" : "  -  ") + o.resolution;
        if (meta.empty() && o.size_bytes >= 0)
          meta = FormatSize(o.size_bytes);
        row.meta = meta;
      }
      entry_rows_.push_back(std::move(row));
    }
  }
  entry_count_ = (int)entry_rows_.size();
  sel_entry_ = std::clamp(sel_entry_, 0, std::max(0, entry_count_ - 1));
  model_.DirtyVariable("entries");
  model_.DirtyVariable("entry_count");
  model_.DirtyVariable("sel_entry");
}

void App::RebuildCrumb() {
  std::string crumb;
  for (const BrowseLevel& level : path_) {
    if (!crumb.empty())
      crumb += "  ›  ";
    crumb += level.title;
  }
  bind_crumb_ = crumb;
  model_.DirtyVariable("crumb");
}

void App::RebuildDetail() {
  const upnp::DidlObject* o = SelectedObject();
  if (!o) {
    bind_detail_title_.clear();
    bind_detail_meta_.clear();
    bind_detail_desc_.clear();
  } else {
    bind_detail_title_ = o->title;

    std::string meta;
    if (o->container) {
      meta = "Folder";
      if (o->child_count >= 0)
        meta += "  -  " + std::to_string(o->child_count) +
          (o->child_count == 1 ? " entry" : " entries");
    } else {
      if (o->IsAudio()) meta = "Audio";
      else if (o->IsVideo()) meta = "Video";
      else if (o->IsImage()) meta = "Image";
      else meta = "Item";
      if (o->duration_us > 0)
        meta += "  -  " + FormatTime(o->duration_us);
      if (o->size_bytes >= 0)
        meta += "  -  " + FormatSize(o->size_bytes);
    }
    bind_detail_meta_ = meta;

    std::string desc;
    auto add = [&desc](const char* label, const std::string& value) {
      if (!value.empty())
        desc += std::string(label) + ": " + value + "\n";
    };
    add("Artist", o->artist);
    add("Album", o->album);
    add("Genre", o->genre);
    add("Date", o->date);
    add("Resolution", o->resolution);
    if (!o->protocol_info.empty()) {
      // protocolInfo is "http-get:*:video/x-matroska:DLNA...."; the MIME
      // type in the third field is the useful part.
      size_t a = o->protocol_info.find(':');
      size_t b = (a == std::string::npos) ? a : o->protocol_info.find(':', a + 1);
      size_t c = (b == std::string::npos) ? b : o->protocol_info.find(':', b + 1);
      if (b != std::string::npos)
        add("Format", o->protocol_info.substr(b + 1,
          (c == std::string::npos ? o->protocol_info.size() : c) - b - 1));
    }
    bind_detail_desc_ = desc;
  }
  model_.DirtyVariable("detail_title");
  model_.DirtyVariable("detail_meta");
  model_.DirtyVariable("detail_desc");
  RefreshArtBindings();
}

void App::MoveSelection(int delta) {
  if (entry_count_ == 0)
    return;
  sel_entry_ = std::clamp(sel_entry_ + delta, 0, entry_count_ - 1);
  if (BrowseLevel* level = CurrentLevel())
    level->selection = sel_entry_;
  model_.DirtyVariable("sel_entry");
  RebuildDetail();
  scroll_entries_pending_ = true;
}

void App::ActivateSelection() {
  const upnp::DidlObject* o = SelectedObject();
  if (!o)
    return;
  if (o->container) {
    EnterContainer(*o);
  } else if (!o->res_url.empty()) {
    PlayObject(*o);
  } else {
    ShowToast("This item has nothing to play");
  }
}

void App::HandleKeyBrowse(Rml::Event& event, int key) {
  switch (key) {
  case Rml::Input::KI_UP:
    MoveSelection(-1);
    break;
  case Rml::Input::KI_DOWN:
    MoveSelection(+1);
    break;
  case Rml::Input::KI_PRIOR: // page up / left shoulder
    MoveSelection(-10);
    break;
  case Rml::Input::KI_NEXT:  // page down / right shoulder
    MoveSelection(+10);
    break;
  case Rml::Input::KI_HOME:
    MoveSelection(-entry_count_);
    break;
  case Rml::Input::KI_END:
    MoveSelection(+entry_count_);
    break;
  case Rml::Input::KI_RETURN:
  case Rml::Input::KI_NUMPADENTER:
    ActivateSelection();
    break;
  case Rml::Input::KI_BACK:   // backspace / circle
  case Rml::Input::KI_ESCAPE:
    LeaveContainer();
    break;
  default:
    return;
  }
  event.StopPropagation();
}
