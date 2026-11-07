#pragma once

#include <winrt/Microsoft.UI.Dispatching.h>

#include <array>
#include <chrono>
#include <memory>

#include "icon_view.hpp"
#include "menu_material.hpp"
#include "view_context.hpp"

namespace kr::win {

// Zen's sidebar media controls (ZenMediaController.mjs): one card per media
// session or capture, newest in front, older ones peeking above it. Hovering
// fans the stack out and reveals each card's title and position. Cards are
// created on first use and reused; the position clock ticks only while the
// stack is expanded and something plays. Horizontal layouts reuse the same
// cards in a transient dropdown beneath the media toolbar button.
class MediaStack {
 public:
  explicit MediaStack(ViewContext& context);
  ~MediaStack();
  MediaStack(const MediaStack&) = delete;
  MediaStack& operator=(const MediaStack&) = delete;

  void Render(const ShellViewModel& vm);
  void SetCompact(bool compact);
  void SetHorizontal(bool horizontal, const controls::Grid& sidebar);
  void SetObscured(bool obscured);
  void SetText(UiTextView text);
  void RefreshPointerHover() noexcept;
  void DismissHover() noexcept;
  [[nodiscard]] bool HasActivePopup() const noexcept {
    return horizontal_ && (dropdown_open_ || closing_);
  }
  [[nodiscard]] const controls::Button& ToolbarButton() const noexcept {
    return toolbar_button_->control;
  }
  // Overlays the tab list, bottom-aligned above the footer.
  [[nodiscard]] const controls::StackPanel& Root() const noexcept {
    return root_;
  }
  // Reserves the collapsed stack's height below the tab list.
  [[nodiscard]] const controls::Border& Spacer() const noexcept {
    return spacer_;
  }

 private:
  struct Card;
  void Expand(bool expanded) noexcept;
  void Restack(bool animate);
  void Tick();
  void UpdateClock();
  void UpdateVisibility();
  void OpenDropdown(bool keyboard = false,
                    bool explicit_request = false) noexcept;
  void CloseDropdown() noexcept;
  void ScheduleClose();
  void StyleDropdown();
  void RefreshDropdownBackground() noexcept;
  void DropdownClosed() noexcept;
  [[nodiscard]] bool CanOpen(bool explicit_request = false) const;
  [[nodiscard]] bool PointerOverDropdown() const;

  ViewContext& context_;
  controls::StackPanel root_;
  controls::Border spacer_;
  std::unique_ptr<NativeButton> toolbar_button_;
  controls::Flyout dropdown_{nullptr};
  controls::ScrollViewer dropdown_scroll_{nullptr};
  controls::FlyoutPresenter dropdown_presenter_{nullptr};
  xaml::Style dropdown_style_{nullptr};
  MenuMaterial dropdown_material_;
  bool horizontal_ = false, dropdown_open_ = false, dropdown_allowed_ = false,
       dropdown_keyboard_ = false, closing_ = false, dismissed_hover_ = false,
       chrome_visible_ = false;
  std::array<std::unique_ptr<Card>, 3> cards_;  // front first
  std::size_t shown_ = 0;
  bool expanded_ = false, compact_ = false;
  UiTextView text_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer clock_{nullptr};
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer close_timer_{nullptr};
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker tick_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker
      close_tick_;
  controls::Primitives::FlyoutBase::Closed_revoker dropdown_closed_;
  controls::Primitives::FlyoutBase::Opened_revoker dropdown_opened_;
  xaml::FrameworkElement::SizeChanged_revoker dropdown_sized_;
  xaml::UIElement::PointerEntered_revoker entered_, button_entered_,
      dropdown_entered_;
  xaml::UIElement::PointerExited_revoker exited_, button_exited_,
      dropdown_exited_;
};
}  // namespace kr::win
