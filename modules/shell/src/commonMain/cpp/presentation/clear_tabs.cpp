#include "krypton/ui/presentation/clear_tabs.hpp"

#include <algorithm>

#include "krypton/ui/state/store.hpp"

namespace kr {

namespace {

// The presenter's Today section: the active space's tabs that are neither
// favorites nor pinned. A host without spaces lists every tab there.
[[nodiscard]] bool Unpinned(const Tab& tab, SpaceId space) noexcept {
  return !tab.flags.HasAny(Flags<TabFlag>(TabFlag::kFavorite) |
                           TabFlag::kPinned) &&
         (!space || tab.space == space);
}

[[nodiscard]] bool InUse(const Tab& tab, const Store& store) noexcept {
  const auto busy = [](const Tab& value) {
    return value.flags.HasAny(Flags<TabFlag>(TabFlag::kAudible) |
                              TabFlag::kRecording | TabFlag::kCapturing);
  };
  if (tab.id == store.presented_tab() || busy(tab))
    return true;
  if (const auto* group = store.SplitForTab(tab.id)) {
    for (const auto member : group->tabs()) {
      if (member == store.presented_tab())
        return true;
      if (const auto* value = store.FindTab(member); value && busy(*value))
        return true;
    }
  }
  return false;
}

}  // namespace

bool UnpinnedTabsIdle(const Store& store) {
  bool any = false;
  for (const Tab& tab : store.tabs()) {
    if (!Unpinned(tab, store.active_space()) || store.IsTransientTab(tab.id))
      continue;
    if (InUse(tab, store))
      return false;
    any = true;
  }
  return any;
}

std::vector<TabId> TabsToClear(const Store& store) {
  const SpaceId space = store.active_space();
  const TabId active = store.active_tab();
  std::vector<TabId> idle, all;
  for (const Tab& tab : store.tabs()) {
    if (!Unpinned(tab, space) || store.IsTransientTab(tab.id))
      continue;
    all.push_back(tab.id);
    if (!InUse(tab, store))
      idle.push_back(tab.id);
  }
  if (!idle.empty())
    return idle;
  // Closing the selected tab last lets the browser choose its successor once.
  std::ranges::stable_partition(all,
                                [active](TabId id) { return id != active; });
  return all;
}

}  // namespace kr
