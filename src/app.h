// SPDX-License-Identifier: GPL-3.0-or-later
//
// Application shell, structured after ps5-payload-dev/tvhp: all RmlUi access
// happens on the main thread; anything that touches the network (SSDP,
// SOAP, opening a stream) runs on a single worker thread whose results are
// polled once per frame in Update().
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <RmlUi/Core.h>

#include "player.h"
#include "upnp/dlna.h"

class App : public Rml::EventListener {
public:
  App();
  ~App() override;

  struct Options {
    std::string assets_dir = "assets"; // directory containing main.rml
    std::string cache_dir;             // artwork cache; "" = XDG default
  };

  // Creates the data model and loads <assets_dir>/main.rml. Must be called
  // before the first context update.
  bool Initialize(Rml::Context* context, const Options& options, std::string& error);
  void Shutdown();

  // Per-frame housekeeping; call before context->Update().
  void Update();

  // Hook for the player to draw video beneath the UI; call between
  // Backend::BeginFrame() and context->Render().
  void RenderVideo(int width, int height);

  // Rml::EventListener (document-level keydown, capture phase).
  void ProcessEvent(Rml::Event& event) override;

private:
  enum class View { Servers, Browse };

  // One level of the browse tree; kept on a stack so backing out is
  // instant and restores the selection.
  struct BrowseLevel {
    std::string object_id;
    std::string title;
    std::vector<upnp::DidlObject> objects;
    int selection = 0;
  };

  // Rows exposed to the RmlUi data model.
  struct ServerRow {
    Rml::String name;
    Rml::String detail;
  };
  struct EntryRow {
    Rml::String icon;   // glyph for the kind of entry
    Rml::String title;
    Rml::String meta;   // duration / child count / codec hints
    bool folder = false;
  };

  bool SetupDataModel(Rml::Context* context, std::string& error);

  // --- Worker ------------------------------------------------------------
  // Tasks run strictly in order on one background thread; that thread is
  // the only one allowed to call into upnp:: or player_->Open()/Stop().
  void PostTask(std::function<void()> task);
  void WorkerMain();

  // --- Servers view ------------------------------------------------------
  void StartDiscovery();
  void RebuildServerRows();
  void HandleKeyServers(Rml::Event& event, int key);

  // --- Browse view -------------------------------------------------------
  void OpenServer(int index);            // browse the root of a server
  void EnterContainer(const upnp::DidlObject& obj);
  void LeaveContainer();                 // back one level (or to servers)
  void RequestBrowse(const std::string& object_id, const std::string& title);
  void RebuildEntryRows();
  void RebuildDetail();
  void RebuildCrumb();
  void MoveSelection(int delta);
  void ActivateSelection();
  void HandleKeyBrowse(Rml::Event& event, int key);
  const upnp::DidlObject* SelectedObject() const;
  BrowseLevel* CurrentLevel();

  // --- Playback ----------------------------------------------------------
  void PlayObject(const upnp::DidlObject& obj);
  void StopPlayback();                   // posts the stop; exits watch UI
  void EnterWatch(const upnp::DidlObject& obj);
  void ExitWatch();
  void ShowWatchInfo(double seconds);
  void UpdateWatchOverlay();
  // AtEnd(): audio advances to the next track in the folder, video exits.
  void HandlePlaybackEnd();
  bool PlayNeighbor(int direction);      // next/previous playable item
  void CycleAudioTrack();                // triangle: next audio track
  void HandleKeyWatch(Rml::Event& event, int key);

  void EnsureRowVisible(const char* list_id, int index, float row_pitch);
  void ShowToast(const std::string& text);

  // --- Artwork -----------------------------------------------------------
  // Covers and thumbnails are plain http URLs in the DIDL metadata; RmlUi
  // only loads textures through the file interface, so the worker downloads
  // each one into a small on-disk cache and the UI binds the local path.
  // Returns the cached path, or "" while the download is (or has been
  // scheduled to be) fetched in the background, or if it failed.
  std::string ArtPathFor(const std::string& url);
  void RefreshArtBindings();       // re-resolve detail/now-playing art

  // Data model bound state (main thread only).
  Rml::DataModelHandle model_;
  Rml::String bind_view_ = "servers";
  Rml::String bind_status_;       // servers view status line
  Rml::String bind_toast_;
  Rml::String bind_crumb_;        // breadcrumb of the browse path
  Rml::String bind_server_name_;  // topbar: connected server
  Rml::String bind_clock_;
  Rml::String bind_detail_title_;
  Rml::String bind_detail_meta_;
  Rml::String bind_detail_desc_;
  Rml::String bind_player_status_;
  bool bind_busy_ = false;         // a browse/discovery is in flight
  bool bind_watching_ = false;     // full-screen playback, chrome hidden
  bool bind_info_visible_ = false; // watch info bar shown (auto-hides)
  bool bind_watch_audio_ = false;  // audio-only: persistent now-playing card
  bool bind_watch_paused_ = false;
  bool bind_watch_seekable_ = false;
  Rml::String bind_watch_title_;
  Rml::String bind_watch_meta_;
  Rml::String bind_watch_time_;
  Rml::String bind_watch_progress_ = "0%"; // data-style-width; never empty
  Rml::String bind_watch_atrack_;  // active audio track label; "" when the
                                   // file has fewer than two audio tracks
  Rml::String bind_detail_art_;    // local cached image path, "" = none
  Rml::String bind_np_art_;        // now-playing artwork path, "" = none

  std::vector<ServerRow> server_rows_;
  std::vector<EntryRow> entry_rows_;
  int sel_server_ = 0;
  int server_count_ = 0;
  int sel_entry_ = 0;
  int entry_count_ = 0;

  // Backing data (main thread).
  std::vector<upnp::MediaServer> servers_;
  int current_server_ = -1;
  std::vector<BrowseLevel> path_;   // root first
  upnp::DidlObject playing_;        // item in the player right now

  std::unique_ptr<Player> player_;
  Rml::Context* context_ = nullptr;
  Rml::ElementDocument* document_ = nullptr;
  View view_ = View::Servers;

  // Worker plumbing.
  std::thread worker_;
  std::mutex tasks_mutex_;
  std::condition_variable tasks_cv_;
  std::deque<std::function<void()>> tasks_;
  std::atomic<bool> worker_running_{false};

  // Results the worker leaves for Update() to pick up.
  struct Pending {
    std::mutex mutex;
    bool servers_ready = false;
    std::vector<upnp::MediaServer> servers;
    std::string discover_error;
    bool browse_ready = false;
    uint32_t browse_request = 0;    // matches browse_request_ or is stale
    std::string browse_object_id;
    std::string browse_title;
    upnp::BrowseResult browse;
    std::string browse_error;
    bool play_ready = false;
    bool play_ok = false;
    std::string play_error;
    // url -> cached local path ("" = download failed)
    std::vector<std::pair<std::string, std::string>> art;
  };
  Pending pending_;

  // Artwork cache (main thread).
  std::string art_dir_;                          // "" = cache unavailable
  std::map<std::string, std::string> art_paths_; // url -> path ("" = failed)
  std::set<std::string> art_inflight_;
  uint32_t browse_request_ = 0;     // id of the browse we are waiting for
  std::atomic<int> busy_ops_{0};

  double toast_deadline_ = 0.0;
  double info_deadline_ = 0.0;      // watch info bar auto-hide
  bool scroll_entries_pending_ = false;
  bool scroll_servers_pending_ = false;
};
