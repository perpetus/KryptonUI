#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "krypton/ui/model/model.hpp"

namespace kr {
// Bound both native controls and decoded snapshot memory, independently of the
// number of tabs. Other pages are requested only when selection reaches them.
inline constexpr std::size_t kTabSwitcherPageSize = 5;
struct TabSwitcherState {
  bool open = false;
  std::vector<TabId> tabs;
  std::size_t selected = 0;
  std::uint32_t request = 0;
  std::array<TabPreviewData, kTabSwitcherPageSize> previews;
};
}  // namespace kr
