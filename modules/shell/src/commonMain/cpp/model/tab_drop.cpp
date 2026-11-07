#include "krypton/ui/model/tab_drop.hpp"

namespace kr {

std::optional<std::size_t> TabDropIndex(std::span<const Tab> tabs,
                                        TabId tab,
                                        TabId previous,
                                        TabId next) noexcept {
  // Positions count the other tabs only: the host removes the moved tab
  // before it inserts it at the final index.
  std::optional<std::size_t> after;
  std::size_t position = 0;
  for (const Tab& item : tabs) {
    if (item.id == tab)
      continue;
    if (next && item.id == next)
      return position;
    ++position;
    if (previous && item.id == previous)
      after = position;
  }
  return after;
}

}  // namespace kr
