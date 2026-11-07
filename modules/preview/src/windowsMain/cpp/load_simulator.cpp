#include "load_simulator.hpp"

#include <algorithm>

#include "krypton/ui/design/tokens.generated.hpp"

namespace kr::preview {
bool LoadSimulator::Arm(HWND window) {
  return SetTimer(window, kTimer,
                  static_cast<UINT>(
                      design::kTokens.loading_progress.progress_duration_ms),
                  nullptr) != 0;
}
bool LoadSimulator::Start(HWND window, KrTabId tab, std::uint64_t navigation) {
  std::erase_if(pending_,
                [tab](const Pending& entry) { return entry.tab == tab; });
  pending_.push_back({tab, navigation});
  if (Arm(window))
    return true;
  pending_.pop_back();
  return false;
}
void LoadSimulator::Step(HWND window, fixtures::MockBrowser& browser) {
  KillTimer(window, kTimer);
  std::erase_if(pending_, [&browser](const Pending& entry) {
    return !browser.AdvanceLoading(entry.tab, entry.navigation);
  });
  if (!pending_.empty() && !Arm(window)) {
    // A failed timer must not leave the fixture's page perpetually loading.
    for (const auto& entry : pending_)
      while (browser.AdvanceLoading(entry.tab, entry.navigation)) {
      }
    pending_.clear();
  }
}
void LoadSimulator::Stop(HWND window) noexcept {
  if (window)
    KillTimer(window, kTimer);
  pending_.clear();
}
}  // namespace kr::preview
