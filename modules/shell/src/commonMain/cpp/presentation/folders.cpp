#include <algorithm>
#include <limits>
#include <numeric>

#include "krypton/ui/presentation/presenter.hpp"

namespace kr {
void Presenter::PresentFolders(const Store& store) {
  constexpr auto none = std::numeric_limits<std::size_t>::max();
  std::size_t row_count = 0;
  bool changed = false;
  const auto emit = [&](FolderRowVm row) {
    if (row_count == folder_rows_.size()) {
      folder_rows_.push_back(row);
      changed = true;
    } else {
      auto& previous = folder_rows_[row_count];
      if (previous.folder != row.folder || previous.tab != row.tab ||
          previous.depth != row.depth) {
        previous = row;
        changed = true;
      }
      previous.contains_active = row.contains_active;
    }
    ++row_count;
  };
  const auto folders = store.folders();
  folder_links_.assign(folders.size(), {none, none, none, none, false});
  folder_order_.resize(folders.size());
  std::iota(folder_order_.begin(), folder_order_.end(), std::size_t{0});
  std::ranges::sort(folder_order_, [&](std::size_t a, std::size_t b) {
    return folders[a].order != folders[b].order
               ? folders[a].order < folders[b].order
               : folders[a].id < folders[b].id;
  });
  std::size_t root = none;
  // Prepend in reverse order to keep sibling order without allocating lists.
  for (auto at = folder_order_.rbegin(); at != folder_order_.rend(); ++at) {
    const auto i = *at;
    if (folders[i].space != store.active_space())
      continue;
    const auto parent = store.FolderIndex(folders[i].parent);
    auto& first = parent ? folder_links_[*parent].child : root;
    folder_links_[i].sibling = first;
    first = i;
  }
  folder_next_tab_.assign(folder_tabs_.size(), none);
  for (std::size_t i = 0; i < folder_tabs_.size(); ++i) {
    const auto index = store.FolderIndex(folder_tabs_[i].folder);
    if (!index)
      continue;
    auto& link = folder_links_[*index];
    if (link.last_tab == none)
      link.tab = i;
    else
      folder_next_tab_[link.last_tab] = i;
    link.last_tab = i;
    if (folder_tabs_[i].active) {
      auto ancestor = index;
      for (std::size_t depth = 0; ancestor && depth < kMaxFolderDepth;
           ++depth) {
        folder_links_[*ancestor].active = true;
        ancestor = store.FolderIndex(folders[*ancestor].parent);
      }
    }
  }
  const auto append = [&](auto&& self, std::size_t first, std::size_t depth,
                          bool active_only) -> void {
    for (auto i = first; i != none; i = folder_links_[i].sibling) {
      const auto& link = folder_links_[i];
      if (active_only && !link.active)
        continue;
      emit({folders[i].id, {}, depth, link.active});
      const bool collapsed = active_only || folders[i].collapsed;
      self(self, link.child, depth + 1, collapsed);
      for (auto tab = link.tab; tab != none; tab = folder_next_tab_[tab])
        if (!collapsed || folder_tabs_[tab].active)
          emit({{}, folder_tabs_[tab].id, depth + 1});
    }
  };
  append(append, root, 0, false);
  if (row_count != folder_rows_.size()) {
    folder_rows_.resize(row_count);
    changed = true;
  }
  if (changed)
    vm_.changed |= Dirty::kTabs;
}
}  // namespace kr
