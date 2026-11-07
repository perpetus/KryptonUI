#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "krypton_ui/abi.h"

namespace kr::fixtures {
// Fixture state for downloads, the sidebar media session and the library.
// Everything here is sample data for the preview and tests.
struct LibraryFixture {
  struct Download {
    KrDownloadId id = 0;
    std::uint32_t state = KR_DOWNLOAD_COMPLETE;
    std::uint32_t flags = 0;
    float progress = 0;
    std::string name, status;
    KrImageId icon = 0;
  };
  struct HistoryEntry {
    KrLibraryItemId id = 0;
    std::string title, url, group;
    std::uint32_t color = 0;
  };
  std::vector<Download> downloads;
  std::vector<HistoryEntry> history;
  KrTabId media_tab = 0;
  std::uint32_t media_flags = 0;
  std::size_t track = 0;
  double position = 42;
  bool media_present = false;
  bool media_enabled = false;
  std::array<bool, KR_LIBRARY_ROUTING + 1> visible{};
  std::uint32_t menu_kind = 0;
  std::uint64_t menu_target = 0;
};
}  // namespace kr::fixtures
