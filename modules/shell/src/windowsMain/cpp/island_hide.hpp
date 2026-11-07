#pragma once

#include <windows.h>
#include <winrt/Microsoft.UI.Dispatching.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace kr::win {
// SiteBridge/AppWindow Hide alone can leave WinUI's last mouse point cached.
// Move the live native surface away first, then hide after input has had a
// dispatcher turn to deliver its normal leave. Keep the controls and island.
class IslandHide {
 public:
  IslandHide() = default;
  ~IslandHide() { lifetime_.reset(); }
  IslandHide(const IslandHide&) = delete;
  IslandHide& operator=(const IslandHide&) = delete;

  void Hide(
      HWND window,
      const winrt::Microsoft::UI::Dispatching::DispatcherQueue& dispatcher,
      std::move_only_function<void()> hide);
  // Cancel a stale hide and restore native placement before applying new
  // bounds.
  void BeforeShow();
  [[nodiscard]] bool IsParked() const noexcept { return parked_; }

 private:
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  HWND window_ = nullptr;
  POINT origin_{};
  std::uint64_t generation_ = 0;
  bool parked_ = false, queued_ = false;
};
}  // namespace kr::win
