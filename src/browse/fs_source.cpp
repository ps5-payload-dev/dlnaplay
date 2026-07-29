// SPDX-License-Identifier: GPL-3.0-or-later

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "browse/fs_source.h"

namespace browse {
  namespace {

    std::string LowerExt(const std::string& name) {
      const size_t dot = name.rfind('.');
      if (dot == std::string::npos || dot + 1 >= name.size())
	return {};
      std::string ext = name.substr(dot + 1);
      for (char& c : ext)
	c = (char)std::tolower((unsigned char)c);
      return ext;
    }

    struct ExtInfo {
      Entry::Kind kind;
      const char* mime; // shown in the details pane
    };

    // Extension -> kind/MIME. Deliberately permissive on the video side; ffmpeg
    // will tell us soon enough if it cannot open something.
    const ExtInfo* ClassifyExt(const std::string& ext) {
      static const std::pair<const char*, ExtInfo> kMap[] = {
	// Audio
	{"mp3",  {Entry::Kind::Audio, "audio/mpeg"}},
	{"flac", {Entry::Kind::Audio, "audio/flac"}},
	{"ogg",  {Entry::Kind::Audio, "audio/ogg"}},
	{"oga",  {Entry::Kind::Audio, "audio/ogg"}},
	{"opus", {Entry::Kind::Audio, "audio/opus"}},
	{"m4a",  {Entry::Kind::Audio, "audio/mp4"}},
	{"aac",  {Entry::Kind::Audio, "audio/aac"}},
	{"wav",  {Entry::Kind::Audio, "audio/wav"}},
	{"wma",  {Entry::Kind::Audio, "audio/x-ms-wma"}},
	{"ape",  {Entry::Kind::Audio, "audio/x-ape"}},
	{"mka",  {Entry::Kind::Audio, "audio/x-matroska"}},
	// Video
	{"mkv",  {Entry::Kind::Video, "video/x-matroska"}},
	{"mp4",  {Entry::Kind::Video, "video/mp4"}},
	{"m4v",  {Entry::Kind::Video, "video/mp4"}},
	{"mov",  {Entry::Kind::Video, "video/quicktime"}},
	{"avi",  {Entry::Kind::Video, "video/x-msvideo"}},
	{"webm", {Entry::Kind::Video, "video/webm"}},
	{"ts",   {Entry::Kind::Video, "video/mp2t"}},
	{"m2ts", {Entry::Kind::Video, "video/mp2t"}},
	{"mts",  {Entry::Kind::Video, "video/mp2t"}},
	{"mpg",  {Entry::Kind::Video, "video/mpeg"}},
	{"mpeg", {Entry::Kind::Video, "video/mpeg"}},
	{"vob",  {Entry::Kind::Video, "video/mpeg"}},
	{"wmv",  {Entry::Kind::Video, "video/x-ms-wmv"}},
	{"flv",  {Entry::Kind::Video, "video/x-flv"}},
	{"3gp",  {Entry::Kind::Video, "video/3gpp"}},
	// Images
	{"jpg",  {Entry::Kind::Image, "image/jpeg"}},
	{"jpeg", {Entry::Kind::Image, "image/jpeg"}},
	{"png",  {Entry::Kind::Image, "image/png"}},
	{"gif",  {Entry::Kind::Image, "image/gif"}},
	{"bmp",  {Entry::Kind::Image, "image/bmp"}},
	{"webp", {Entry::Kind::Image, "image/webp"}},
      };
      for (const auto& m : kMap) {
	if (ext == m.first)
	  return &m.second;
      }
      return nullptr;
    }

    // Case-insensitive match against the usual cover art file names.
    bool IsCoverName(const std::string& name) {
      std::string lower = name;
      for (char& c : lower)
	c = (char)std::tolower((unsigned char)c);
      for (const char* c : {"cover.jpg", "cover.jpeg", "cover.png", "folder.jpg",
			    "folder.jpeg", "folder.png", "front.jpg", "front.png",
			    "albumart.jpg", "albumart.png"}) {
	if (lower == c)
	  return true;
      }
      return false;
    }

    bool CaseInsensitiveLess(const std::string& a, const std::string& b) {
      const size_t n = std::min(a.size(), b.size());
      for (size_t i = 0; i < n; i++) {
	const int ca = std::tolower((unsigned char)a[i]);
	const int cb = std::tolower((unsigned char)b[i]);
	if (ca != cb)
	  return ca < cb;
      }
      return a.size() < b.size();
    }

    std::string TrimTrailingSlashes(std::string path) {
      while (path.size() > 1 && path.back() == '/')
	path.pop_back();
      return path;
    }

  }

  FsSource::FsSource(std::string name, std::string root)
    : name_(std::move(name)), root_(TrimTrailingSlashes(std::move(root))) {}

  bool FsSource::Browse(const std::string& id, Listing& out, std::string& error) {
    const std::string dir = TrimTrailingSlashes(id);

    // Ids come back from the UI verbatim, but stay paranoid: only paths
    // inside this source's root are browsable.
    if (dir.compare(0, root_.size(), root_) != 0 ||
	(dir.size() > root_.size() && dir[root_.size()] != '/')) {
      error = "path outside this source";
      return false;
    }

    DIR* d = ::opendir(dir.c_str());
    if (!d) {
      error = "cannot open " + dir + ": " + std::strerror(errno);
      return false;
    }

    std::string cover; // cover art for this directory, if any
    std::vector<Entry> folders;
    std::vector<Entry> files;

    while (struct dirent* de = ::readdir(d)) {
      const std::string name = de->d_name;
      if (name.empty() || name[0] == '.')
	continue;

      const std::string path = dir + "/" + name;
      struct stat st = {};
      if (::stat(path.c_str(), &st) != 0)
	continue;

      if (S_ISDIR(st.st_mode)) {
	Entry e;
	e.id = path;
	e.title = name;
	e.kind = Entry::Kind::Folder;
	folders.push_back(std::move(e));
	continue;
      }
      if (!S_ISREG(st.st_mode))
	continue;

      const ExtInfo* info = ClassifyExt(LowerExt(name));
      if (!info)
	continue;
      if (cover.empty() && IsCoverName(name))
	cover = path;

      Entry e;
      e.id = path;
      e.title = name;
      e.kind = info->kind;
      e.format = info->mime;
      e.res_url = path;
      e.size_bytes = (int64_t)st.st_size;
      if (info->kind == Entry::Kind::Image)
	e.art_url = path; // the image previews itself in the details pane
      files.push_back(std::move(e));
    }
    ::closedir(d);

    // Audio tracks inherit the directory's cover art, album-folder style.
    if (!cover.empty()) {
      for (Entry& e : files) {
	if (e.IsAudio() && e.art_url.empty())
	  e.art_url = cover;
      }
    }

    auto by_title = [](const Entry& a, const Entry& b) {
      return CaseInsensitiveLess(a.title, b.title);
    };
    std::sort(folders.begin(), folders.end(), by_title);
    std::sort(files.begin(), files.end(), by_title);

    out.entries = std::move(folders);
    out.entries.insert(out.entries.end(),
		       std::make_move_iterator(files.begin()),
		       std::make_move_iterator(files.end()));
    return true;
  }
}
