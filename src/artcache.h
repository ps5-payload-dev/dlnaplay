// SPDX-License-Identifier: GPL-3.0-or-later
//
// Artwork cache: downloads album covers / thumbnails announced in DIDL
// metadata, downscales them, and stores them as PNG files that the RmlUi
// SDL renderer can load with a plain <img src>.
//
// Fetch() does blocking network I/O and image decoding; the app calls it
// from its worker thread only. Returned paths are stable for the lifetime
// of the process (the cache directory is cleaned up on Shutdown).
#ifndef ARTCACHE_H
#define ARTCACHE_H

#include <string>

namespace artcache {

// Creates the per-run cache directory. Returns false if no writable
// location exists (the app then simply runs without artwork).
bool Initialize();

// Removes the cache directory and everything in it.
void Shutdown();

// Fetches 'url', decodes it, scales it down so the longest side is at most
// 'max_px', and returns the path of the cached PNG ("" on any failure).
// Results are cached by (url, max_px): repeated calls are free.
std::string Fetch(const std::string& url, int max_px);

} // namespace artcache

#endif
