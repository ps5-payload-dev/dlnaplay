// SPDX-License-Identifier: GPL-3.0-or-later
//
// Constants and helpers shared between the App translation units.
#ifndef APP_INTERNAL_H
#define APP_INTERNAL_H

#include <cstdint>
#include <cstdio>
#include <string>

#include <RmlUi/Core.h>

namespace appdetail {

// Row geometry; must match the stylesheets (.source-row / .entry-row height
// + margin-bottom) so keyboard scrolling can be computed without layout
// queries on the generated rows.
inline constexpr float kSourceRowPitch = 96.0f + 12.0f;
inline constexpr float kEntryRowPitch = 76.0f + 10.0f;

inline constexpr double kToastSec = 4.0;
inline constexpr double kWatchInfoSec = 5.0;      // watch info bar auto-hide
inline constexpr int kDiscoveryWaitMs = 2500;

inline constexpr int64_t kSeekSmallUs = 10 * 1000000LL;
inline constexpr int64_t kSeekLargeUs = 5* 60 * 1000000LL;

// Seconds since startup, from RmlUi's system interface.
inline double Now()
{
  Rml::SystemInterface* sys = Rml::GetSystemInterface();
  return sys ? sys->GetElapsedTime() : 0.0;
}

// Microseconds -> "1:02:33" / "12:33".
inline std::string FormatTime(int64_t us)
{
  if (us < 0)
    return "--:--";
  const int64_t total = us / 1000000;
  const int h = (int)(total / 3600);
  const int m = (int)((total % 3600) / 60);
  const int s = (int)(total % 60);
  char buf[32];
  if (h > 0)
    std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
  else
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
  return buf;
}

// Bytes -> "1.4 GB".
inline std::string FormatSize(int64_t bytes)
{
  if (bytes < 0)
    return {};
  const char* units[] = {"B", "kB", "MB", "GB", "TB"};
  double v = (double)bytes;
  int u = 0;
  while (v >= 1000.0 && u < 4) {
    v /= 1000.0;
    u++;
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), v < 10 ? "%.1f %s" : "%.0f %s", v, units[u]);
  return buf;
}

} // namespace appdetail

#endif
