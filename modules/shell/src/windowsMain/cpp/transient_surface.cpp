#include "transient_surface.hpp"

#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Windows.Foundation.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
TransientSurface::~TransientSurface() {
  lifetime_.reset();
  input_target_.Reset();
  try {
    Hide();
    if (source_) {
      source_.Content(nullptr);
      source_.Close();
    }
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kError,
                              "Transient popup owner could not close");
  }
}

void TransientSurface::Ensure() {
  if (source_)
    return;
  controls::Grid root;
  root.FlowDirection(xaml::FlowDirection::LeftToRight);
  xaml::Hosting::DesktopWindowXamlSource source;
  const auto island_parent = input_target_.IslandParent(parent_);
  winrt::check_bool(island_parent.has_value());
  source.Initialize(
      winrt::Microsoft::UI::GetWindowIdFromWindow(*island_parent));
  source.ShouldConstrainPopupsToWorkArea(true);
  source.Content(root);
  winrt::check_bool(input_target_
                        .Initialize(winrt::Microsoft::UI::GetWindowFromWindowId(
                                        source.SiteBridge().WindowId()),
                                    root)
                        .has_value());
  root_ = root;
  source_ = source;
}

void TransientSurface::Show(Rect client,
                            float scale,
                            xaml::ElementTheme theme) {
  if (client.empty() || !std::isfinite(scale) || scale <= 0) {
    Hide();
    return;
  }
  Ensure();
  hide_.BeforeShow();
  client_ = client;
  if (root_.RequestedTheme() != theme)
    root_.RequestedTheme(theme);
  const auto px = [scale](float value) {
    return static_cast<std::int32_t>(std::lround(value * scale));
  };
  const auto left = px(client.x), top = px(client.y);
  const winrt::Windows::Graphics::RectInt32 bounds{
      left, top, std::max(1, px(client.right()) - left),
      std::max(1, px(client.bottom()) - top)};
  if (bounds_ != bounds) {
    source_.SiteBridge().MoveAndResize(bounds);
    bounds_ = bounds;
  }
  if (!visible_) {
    const HWND focus = GetFocus();
    if (!input_target_.Owns(focus))
      return_focus_ = focus;
    ++show_generation_;
    source_.SiteBridge().Show();
    visible_ = true;
  }
  Raise();
}

void TransientSurface::Hide(Task on_hidden) noexcept try {
  if (source_ && visible_) {
    const auto bridge = source_.SiteBridge();
    visible_ = false;
    hide_.Hide(Window(), root_.DispatcherQueue(),
               [this, weak = std::weak_ptr<int>(lifetime_), bridge,
                generation = show_generation_,
                on_hidden = std::move(on_hidden)]() mutable {
                 bridge.Hide();
                 // Hiding a popup can restore XAML focus. Return it only
                 // after that handoff, and only if this surface still owns it.
                 if (weak.expired() || generation != show_generation_)
                   return;
                 ReleaseFocus(Window(), std::exchange(return_focus_, nullptr),
                              parent_);
                 if (on_hidden)
                   on_hidden();
               });
    return;
  }
  if (on_hidden && !hide_.IsPending())
    on_hidden();
} catch (...) {
  platform::WriteDefaultLog(LogLevel::kWarning,
                            "Transient popup owner could not hide");
}

void TransientSurface::Raise() noexcept try {
  if (source_ && visible_) {
    input_target_.Raise();
    source_.SiteBridge().MoveInZOrderAtTop();
  }
} catch (...) {
  platform::WriteDefaultLog(LogLevel::kWarning,
                            "Transient popup owner could not be raised");
}

bool TransientSurface::IsReady() const {
  return visible_ && root_ && root_.IsLoaded() && root_.XamlRoot() &&
         !ClientBounds().empty();
}

HWND TransientSurface::Window() const noexcept try {
  return source_ ? winrt::Microsoft::UI::GetWindowFromWindowId(
                       source_.SiteBridge().WindowId())
                 : nullptr;
} catch (...) {
  return nullptr;
}

Rect TransientSurface::ClientBounds() const {
  if (!root_ || !root_.XamlRoot())
    return {client_.x, client_.y, 0, 0};
  const auto size = root_.XamlRoot().Size();
  return {client_.x, client_.y,
          std::max(0.0f, std::min(client_.width, size.Width)),
          std::max(0.0f, std::min(client_.height, size.Height))};
}
}  // namespace kr::win
