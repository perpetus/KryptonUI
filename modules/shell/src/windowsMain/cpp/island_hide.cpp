#include "island_hide.hpp"

#include <algorithm>
#include <utility>

#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
namespace {
constexpr UINT kMoveFlags =
    SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER;
}

void IslandHide::BeforeShow() {
  ++generation_;
  queued_ = false;
  if (parked_ && IsWindow(window_))
    winrt::check_bool(
        SetWindowPos(window_, nullptr, origin_.x, origin_.y, 0, 0, kMoveFlags));
  parked_ = false;
  window_ = nullptr;
}

void IslandHide::Hide(
    HWND window,
    const winrt::Microsoft::UI::Dispatching::DispatcherQueue& dispatcher,
    std::move_only_function<void()> hide) {
  if (queued_)
    return;
  if (!IsWindow(window) || !IsWindowVisible(window) || parked_) {
    hide();
    return;
  }
  RECT rect{};
  winrt::check_bool(GetWindowRect(window, &rect));
  const bool child = (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) != 0;
  origin_ = {rect.left, rect.top};
  if (child)
    winrt::check_bool(ScreenToClient(GetParent(window), &origin_));
  const int width = std::max(1L, rect.right - rect.left);
  const int height = std::max(1L, rect.bottom - rect.top);
  // Native coordinates, outside the entire parent/virtual desktop. This is
  // not a visual metric and does not resize the XAML tree or the renderer.
  const int left =
      (child ? 0 : GetSystemMetrics(SM_XVIRTUALSCREEN)) - width - 1;
  const int top =
      (child ? 0 : GetSystemMetrics(SM_YVIRTUALSCREEN)) - height - 1;
  window_ = window;
  parked_ = true;
  winrt::check_bool(SetWindowPos(window, nullptr, left, top, 0, 0, kMoveFlags));
  queued_ = true;
  const auto generation = ++generation_;
  const std::weak_ptr<int> weak = lifetime_;
  if (!dispatcher.TryEnqueue(
          winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
          [this, weak, generation, hide = std::move(hide)]() mutable {
            if (weak.expired() || generation != generation_)
              return;
            queued_ = false;
            try {
              hide();
            } catch (...) {
              platform::WriteDefaultLog(LogLevel::kWarning,
                                        "Native island could not hide");
            }
          })) {
    queued_ = false;
    // Dispatcher shutdown: the owner will close its native surface. There is
    // no further interactive frame in which a replay could affect hover.
  }
}
}  // namespace kr::win
