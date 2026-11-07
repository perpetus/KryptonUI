#include "tab_preview_surface.hpp"

#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <cmath>

#include "krypton/ui/platform/platform.hpp"
#include "krypton/ui/presentation/tab_preview.hpp"

namespace kr::win {

TabPreviewSurface::~TabPreviewSurface() {
  lifetime_.reset();
  Reset();
}

void TabPreviewSurface::EnsureSource() {
  if (source_)
    return;
  controls::Grid root;
  auto source = xaml::Hosting::DesktopWindowXamlSource();
  const auto island_parent = input_target_.IslandParent(parent_);
  winrt::check_bool(island_parent.has_value());
  source.Initialize(
      winrt::Microsoft::UI::GetWindowIdFromWindow(*island_parent));
  source.Content(root);
  // A hover preview is display-only. The public bridge switch prevents the
  // island from accepting keyboard focus or intercepting page interaction.
  source.SiteBridge().Disable();
  winrt::check_bool(input_target_
                        .Initialize(winrt::Microsoft::UI::GetWindowFromWindowId(
                            source.SiteBridge().WindowId()))
                        .has_value());
  root_ = root;
  source_ = source;
  loaded_ = root_.Loaded(
      winrt::auto_revoke,
      [this, weak = std::weak_ptr<int>(lifetime_)](auto&&, auto&&) {
        if (!weak.expired() && request_)
          Reposition();
      });
}

void TabPreviewSurface::Render(std::uint32_t request,
                               const controls::Border& card,
                               const ShellViewModel& vm,
                               Rect anchor) {
  EnsureSource();
  const bool changed_request = request_ != request;
  if (changed_request)
    fade_.Stop();
  request_ = request;
  client_ = vm.client;
  sidebar_ = vm.layout.sidebar;
  chrome_ = vm.layout.toolbar;
  horizontal_ = vm.layout.tabs != TabLayout::kVertical;
  right_to_left_ = vm.text.right_to_left;
  anchor_ = anchor;
  scale_ = vm.scale_factor;
  on_right_ = vm.layout.sidebar_on_right;
  high_contrast_ = vm.theme.high_contrast;
  const auto theme =
      vm.theme.dark ? xaml::ElementTheme::Dark : xaml::ElementTheme::Light;
  if (root_.RequestedTheme() != theme)
    root_.RequestedTheme(theme);
  const auto direction = vm.text.right_to_left
                             ? xaml::FlowDirection::RightToLeft
                             : xaml::FlowDirection::LeftToRight;
  if (root_.FlowDirection() != direction)
    root_.FlowDirection(direction);
  if (card_ != card) {
    sized_.revoke();
    root_.Children().Clear();
    card_ = card;
    card_.VerticalAlignment(xaml::VerticalAlignment::Top);
    root_.Children().Append(card_);
    sized_ = card_.SizeChanged(
        winrt::auto_revoke,
        [this, weak = std::weak_ptr<int>(lifetime_)](auto&&, auto&&) {
          if (!weak.expired() && request_)
            Reposition();
        });
  }
  Reposition();
  if (changed_request && visible_)
    fade_.Play(card_.Child(), motion_, high_contrast_);
}

void TabPreviewSurface::Reposition() noexcept try {
  if (positioning_ || !request_ || !card_)
    return;
  positioning_ = true;
  struct PositionScope {
    bool& active;
    ~PositionScope() { active = false; }
  } scope{positioning_};
  const auto placement = [this](float height) {
    return horizontal_
               ? PlaceHorizontalTabPreview(client_, chrome_, anchor_, height,
                                           right_to_left_)
               : PlaceTabPreview(client_, sidebar_, anchor_, height, on_right_);
  };
  const auto constraint = placement(client_.height);
  if (constraint.empty()) {
    Close(request_);
    return;
  }
  root_.Measure({constraint.width, constraint.height});
  // A newly connected root can report zero until Loaded. Give it a minimal
  // native frame so Loaded can finish measuring, then resize to the card.
  const auto desired_height = std::max(1 / scale_, root_.DesiredSize().Height);
  const auto frame = placement(desired_height);
  if (frame.empty())
    return;
  // Round the inner-facing edge outwards, so fractional-DIP scaling cannot
  // put even a physical pixel of the transient window over the sidebar.
  const auto left = static_cast<std::int32_t>(std::ceil(frame.x * scale_));
  const auto right =
      static_cast<std::int32_t>(std::floor(frame.right() * scale_));
  const auto top = static_cast<std::int32_t>(std::ceil(frame.y * scale_));
  const auto bottom =
      static_cast<std::int32_t>(std::floor(frame.bottom() * scale_));
  if (right <= left || bottom <= top) {
    Close(request_);
    return;
  }
  const winrt::Windows::Graphics::RectInt32 bounds{left, top, right - left,
                                                   bottom - top};
  if (bounds_ != bounds) {
    bounds_ = bounds;
    source_.SiteBridge().MoveAndResize(bounds);
  }
  if (!visible_) {
    visible_ = true;
    source_.SiteBridge().Show();
  }
  input_target_.Raise();
  source_.SiteBridge().MoveInZOrderAtTop();
}

catch (...) {
  Close(request_);
  platform::WriteDefaultLog(LogLevel::kError,
                            "Tab preview surface could not be positioned");
}

std::chrono::milliseconds TabPreviewSurface::ShowDelay() const noexcept {
  const std::chrono::milliseconds reshow{
      std::lround(kTokens.tab_hover.reshow_within_ms)};
  if (exited_ != std::chrono::steady_clock::time_point{} &&
      std::chrono::steady_clock::now() - exited_ <= reshow)
    return std::chrono::milliseconds::zero();
  return show_delay_;
}

void TabPreviewSurface::Close(std::uint32_t request) noexcept {
  if (!request || request != request_)
    return;
  fade_.Stop();
  request_ = 0;
  visible_ = false;
  try {
    if (source_) {
      ReleaseFocus(winrt::Microsoft::UI::GetWindowFromWindowId(
                       source_.SiteBridge().WindowId()),
                   nullptr, parent_);
      source_.SiteBridge().Hide();
    }
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kError,
                              "Tab preview surface could not hide");
  }
}

void TabPreviewSurface::Reset() noexcept {
  input_target_.Reset();
  loaded_.revoke();
  sized_.revoke();
  Close(request_);
  try {
    if (source_) {
      source_.Content(nullptr);
      source_.Close();
    }
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kError,
                              "Tab preview surface could not close");
  }
  source_ = nullptr;
  root_ = nullptr;
  card_ = nullptr;
  bounds_ = {};
}

}  // namespace kr::win
