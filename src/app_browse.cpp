// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sources view (results from all browse::Providers) and the browse
// view (the content tree of the chosen source). Everything here is
// protocol-agnostic; the per-protocol work lives behind browse::Source.
#include <algorithm>

#include "app.h"
#include "app_internal.h"

using namespace appdetail;

namespace {

// A short glyph for the entry list; the emoji font is loaded as a fallback
// face, so these render everywhere.
Rml::String IconFor(const browse::Entry& e) {
  switch (e.kind) {
  case browse::Entry::Kind::Folder: return "📁";
  case browse::Entry::Kind::Audio:  return "🎵";
  case browse::Entry::Kind::Video:  return "🎬";
  case browse::Entry::Kind::Image:  return "🖼";
  default:                          return "📄";
  }
}

const char* KindLabel(const browse::Entry& e) {
  switch (e.kind) {
  case browse::Entry::Kind::Folder: return "Folder";
  case browse::Entry::Kind::Audio:  return "Audio";
  case browse::Entry::Kind::Video:  return "Video";
  case browse::Entry::Kind::Image:  return "Image";
  default:                          return "Item";
  }
}

} // namespace

// ---------------------------------------------------------------------------
// Sources view
// ---------------------------------------------------------------------------

void App::StartDiscovery() {
  bind_status_ = "Searching for media sources...";
  model_.DirtyVariable("status");

  busy_ops_++;
  PostTask([this] {
    std::vector<browse::SourcePtr> sources;
    std::string errors;
    for (const std::unique_ptr<browse::Provider>& p : providers_) {
      std::string error;
      if (!p->Discover(sources, error) && !error.empty()) {
        if (!errors.empty())
          errors += "; ";
        errors += std::string(p->Name()) + ": " + error;
      }
    }

    {
      std::lock_guard<std::mutex> lock(pending_.mutex);
      pending_.sources_ready = true;
      pending_.sources = std::move(sources);
      pending_.discover_error = errors;
    }
    busy_ops_--;
  });
}

void App::RebuildSourceRows() {
  source_rows_.clear();
  for (const browse::SourcePtr& s : sources_) {
    SourceRow row;
    row.icon = s->Icon();
    row.name = s->Name();
    row.detail = s->Detail();
    source_rows_.push_back(std::move(row));
  }
  source_count_ = (int)sources_.size();
  sel_source_ = std::clamp(sel_source_, 0, std::max(0, source_count_ - 1));
  model_.DirtyVariable("sources");
  model_.DirtyVariable("source_count");
  model_.DirtyVariable("sel_source");
  scroll_sources_pending_ = true;
}

void App::HandleKeySources(Rml::Event& event, int key) {
  switch (key) {
  case Rml::Input::KI_UP:
    sel_source_ = std::max(0, sel_source_ - 1);
    break;
  case Rml::Input::KI_DOWN:
    sel_source_ = std::min(std::max(0, source_count_ - 1), sel_source_ + 1);
    break;
  case Rml::Input::KI_SPACE:
    StartDiscovery();
    event.StopPropagation();
    return;
  case Rml::Input::KI_RETURN:
  case Rml::Input::KI_NUMPADENTER:
    if (sel_source_ < (int)sources_.size())
      OpenSource(sel_source_);
    event.StopPropagation();
    return;
  default:
    return;
  }
  model_.DirtyVariable("sel_source");
  scroll_sources_pending_ = true;
  event.StopPropagation();
}

// ---------------------------------------------------------------------------
// Browse view
// ---------------------------------------------------------------------------

App::BrowseLevel* App::CurrentLevel() {
  return path_.empty() ? nullptr : &path_.back();
}

const browse::Entry* App::SelectedEntry() const {
  if (path_.empty())
    return nullptr;
  const BrowseLevel& level = path_.back();
  if (sel_entry_ < 0 || sel_entry_ >= (int)level.entries.size())
    return nullptr;
  return &level.entries[sel_entry_];
}

void App::OpenSource(int index) {
  if (index < 0 || index >= (int)sources_.size())
    return;
  current_source_ = sources_[index];
  path_.clear();
  sel_entry_ = 0;

  bind_source_name_ = current_source_->Name();
  model_.DirtyVariable("source_name");

  view_ = View::Browse;
  bind_view_ = "browse";
  model_.DirtyVariable("view");

  RebuildEntryRows();
  RebuildCrumb();
  RebuildDetail();
  RequestBrowse(current_source_->RootId(), current_source_->Name());
}

void App::RequestBrowse(const std::string& id, const std::string& title) {
  if (!current_source_)
    return;

  const uint32_t request = ++browse_request_;
  const browse::SourcePtr source = current_source_;

  busy_ops_++;
  PostTask([this, request, source, id, title] {
    browse::Listing result;
    std::string error;
    source->Browse(id, result, error);
    {
      std::lock_guard<std::mutex> lock(pending_.mutex);
      pending_.browse_ready = true;
      pending_.browse_request = request;
      pending_.browse_id = id;
      pending_.browse_title = title;
      pending_.browse = std::move(result);
      pending_.browse_error = error;
    }
    busy_ops_--;
  });
}

void App::EnterContainer(const browse::Entry& entry) {
  if (BrowseLevel* level = CurrentLevel())
    level->selection = sel_entry_;
  RequestBrowse(entry.id, entry.title);
}

void App::LeaveContainer() {
  browse_request_++; // invalidate any browse that is still in flight
  if (path_.size() <= 1) {
    // At the root: back to the source list.
    path_.clear();
    current_source_.reset();
    view_ = View::Sources;
    bind_view_ = "sources";
    model_.DirtyVariable("view");
    scroll_sources_pending_ = true;
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
    for (const browse::Entry& e : level->entries) {
      EntryRow row;
      row.icon = IconFor(e);
      row.title = e.title.empty() ? "(untitled)" : e.title;
      row.folder = e.IsFolder();
      if (e.IsFolder()) {
        if (e.child_count >= 0)
          row.meta = std::to_string(e.child_count) +
            (e.child_count == 1 ? " entry" : " entries");
      } else {
        std::string meta;
        if (e.duration_us > 0)
          meta = FormatTime(e.duration_us);
        if (!e.resolution.empty())
          meta += (meta.empty() ? "" : "  -  ") + e.resolution;
        if (meta.empty() && e.size_bytes >= 0)
          meta = FormatSize(e.size_bytes);
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
  const browse::Entry* e = SelectedEntry();
  if (!e) {
    bind_detail_title_.clear();
    bind_detail_meta_.clear();
    bind_detail_desc_.clear();
  } else {
    bind_detail_title_ = e->title;

    std::string meta = KindLabel(*e);
    if (e->IsFolder()) {
      if (e->child_count >= 0)
        meta += "  -  " + std::to_string(e->child_count) +
          (e->child_count == 1 ? " entry" : " entries");
    } else {
      if (e->duration_us > 0)
        meta += "  -  " + FormatTime(e->duration_us);
      if (e->size_bytes >= 0)
        meta += "  -  " + FormatSize(e->size_bytes);
    }
    bind_detail_meta_ = meta;

    std::string desc;
    auto add = [&desc](const char* label, const std::string& value) {
      if (!value.empty())
        desc += std::string(label) + ": " + value + "\n";
    };
    add("Artist", e->artist);
    add("Album", e->album);
    add("Genre", e->genre);
    add("Date", e->date);
    add("Resolution", e->resolution);
    add("Format", e->format);
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
  const browse::Entry* e = SelectedEntry();
  if (!e)
    return;
  if (e->IsFolder()) {
    EnterContainer(*e);
  } else if (e->IsPlayable()) {
    PlayEntry(*e);
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
