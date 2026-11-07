#pragma once

#include <vector>

#include "krypton/ui/model/model.hpp"

namespace kr {

class Store;

// Zen's Clear button (ZenSpaceManager.closeAllUnpinnedTabs) closes the active
// space's unpinned tabs, keeping those still in use: the selected tab and tabs
// playing audio, recording or sharing a screen. When every one is in use, it
// closes them all.

// True when the active space has unpinned tabs and none is in use; Zen then
// shows the Clear button's arrow.
[[nodiscard]] bool UnpinnedTabsIdle(const Store& store);

// The tabs Clear closes, in host order with the selected tab last.
[[nodiscard]] std::vector<TabId> TabsToClear(const Store& store);

}  // namespace kr
