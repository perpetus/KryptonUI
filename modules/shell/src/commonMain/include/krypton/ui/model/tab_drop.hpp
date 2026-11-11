#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "krypton/ui/core/flags.hpp"
#include "krypton/ui/model/model.hpp"

namespace kr {

// Where a dragged tab lands, as the final index in `tabs` that a host move
// takes: directly before `next`, or directly after `previous` when `next` is
// not listed. Nothing when neither neighbour is listed.
[[nodiscard]] std::optional<std::size_t> TabDropIndex(std::span<const Tab> tabs,
                                                      TabId tab,
                                                      TabId previous,
                                                      TabId next) noexcept;

// The sidebar section that receives a dropped tab. Each one promotes or
// demotes the tab as Zen's moveToAnotherTabContainerIfNecessary does.
enum class TabDropSection : std::uint8_t {
  kKeep,        // a list without sections, such as the horizontal strip
  kEssentials,  // the Essentials grid shared by every space
  kPinned,      // the space's pinned tabs and folders
  kRegular,     // the space's unpinned tabs
};

struct TabFlagChange {
  Flags<TabFlag> mask;
  Flags<TabFlag> values;
};

// The flags a drop into `section` changes for a tab that has `flags`:
// Essentials only gain the Essential flag; the pinned section drops it and
// pins; the regular section drops both.
[[nodiscard]] constexpr TabFlagChange TabDropFlags(
    Flags<TabFlag> flags,
    TabDropSection section) noexcept {
  TabFlagChange change;
  const bool essential = flags.Has(TabFlag::kFavorite);
  const bool pinned = flags.Has(TabFlag::kPinned);
  switch (section) {
    case TabDropSection::kKeep:
      break;
    case TabDropSection::kEssentials:
      if (!essential) {
        change.mask.Set(TabFlag::kFavorite);
        change.values.Set(TabFlag::kFavorite);
      }
      break;
    case TabDropSection::kPinned:
      if (essential)
        change.mask.Set(TabFlag::kFavorite);
      if (!pinned) {
        change.mask.Set(TabFlag::kPinned);
        change.values.Set(TabFlag::kPinned);
      }
      break;
    case TabDropSection::kRegular:
      if (essential)
        change.mask.Set(TabFlag::kFavorite);
      if (pinned)
        change.mask.Set(TabFlag::kPinned);
      break;
  }
  return change;
}

}  // namespace kr
