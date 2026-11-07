#pragma once

#include <windows.h>

#include <vector>

#include "krypton/fixtures/mock_browser.hpp"

namespace kr::preview {
// A bounded source of fake browser events. No timer exists when the queue is
// empty, and production shell animation never uses this host-side adapter.
class LoadSimulator {
 public:
  static constexpr UINT_PTR kTimer = 3;
  [[nodiscard]] bool Start(HWND window, KrTabId tab, std::uint64_t navigation);
  void Step(HWND window, fixtures::MockBrowser& browser);
  void Stop(HWND window) noexcept;

 private:
  struct Pending {
    KrTabId tab;
    std::uint64_t navigation;
  };
  [[nodiscard]] bool Arm(HWND window);
  std::vector<Pending> pending_;
};
}  // namespace kr::preview
