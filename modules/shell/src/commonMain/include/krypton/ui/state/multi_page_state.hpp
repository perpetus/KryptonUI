#pragma once

#include <unordered_map>

#include "krypton/ui/model/multi_page.hpp"

namespace kr {
// Allocated only after the first multi-page update. The disabled path owns no
// containers, resources, tasks, timers or additional tab-index entries.
struct MultiPageState {
  std::unordered_map<SplitGroupId, SplitGroup> groups;
  std::unordered_map<TabId, SplitGroupId> membership;
  GlanceState glance;
};
}  // namespace kr
