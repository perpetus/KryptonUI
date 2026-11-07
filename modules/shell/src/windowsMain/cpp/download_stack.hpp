#pragma once

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Dispatching.h>

#include <array>
#include <memory>

#include "library_visuals.hpp"
#include "media_stack.hpp"
#include "menu_material.hpp"

namespace kr::win {

// Zen's ZenLibraryWidget: hovering the library button reveals the newest
// downloads above the sidebar's footer, and the button carries a progress
// badge while the newest download is pending. Entries are created on first
// reveal; every motion runs on the compositor.
class DownloadStack {
 public:
  DownloadStack(ViewContext& context,
                const controls::Button& library_button,
                const xaml::UIElement& tabs,
                MediaStack& media,
                const xaml::FrameworkElement& media_spacer);
  ~DownloadStack();
  DownloadStack(const DownloadStack&) = delete;
  DownloadStack& operator=(const DownloadStack&) = delete;

  void Render(const ShellViewModel& vm);
  void SetCompact(bool compact);
  void SetHorizontal(bool horizontal, const controls::Grid& sidebar);
  void SetText(UiTextView text);
  void RefreshPointerHover() noexcept;
  void DismissHover() noexcept;
  [[nodiscard]] bool HasActivePopup() const noexcept {
    return horizontal_ && (open_ || closing_);
  }
  // The list, laid over the tab list and media rows, bottom-aligned.
  [[nodiscard]] const controls::StackPanel& List() const noexcept {
    return list_;
  }
  // The button badge, laid over the library button's cell. A Canvas, so the
  // badge may reach past the button like Zen's.
  [[nodiscard]] const controls::Canvas& Badge() const noexcept {
    return badge_host_;
  }

 private:
  void Open() noexcept;
  void Close() noexcept;
  void ScheduleClose();
  void Animate(bool open, std::size_t already_visible = 0);
  void UpdateClip() noexcept;
  void EnsureEntries();
  void StyleDropdown();
  void RefreshDropdownBackground() noexcept;
  void DropdownClosed() noexcept;
  [[nodiscard]] bool PointerOverPreview() const;
  [[nodiscard]] bool CanOpen() const;

  ViewContext& context_;
  controls::Button library_button_;
  xaml::UIElement tabs_;
  MediaStack& media_;
  xaml::FrameworkElement media_spacer_;
  controls::StackPanel list_;
  controls::Flyout dropdown_{nullptr};
  controls::FlyoutPresenter dropdown_presenter_{nullptr};
  xaml::Style dropdown_style_{nullptr};
  xaml::ElementTheme dropdown_theme_ = xaml::ElementTheme::Default;
  MenuMaterial dropdown_material_;
  controls::Primitives::FlyoutBase::Closed_revoker dropdown_closed_;
  controls::Primitives::FlyoutBase::Opened_revoker dropdown_opened_;
  bool horizontal_ = false, library_open_ = false, closing_ = false,
       dismissed_hover_ = false, chrome_visible_ = false;
  controls::Canvas badge_host_;
  controls::Grid badge_;
  std::unique_ptr<DownloadBadge> button_badge_;
  std::array<std::unique_ptr<DownloadEntry>, 4> entries_;
  std::size_t shown_ = 0;
  float clipped_ = -1;
  bool open_ = false, compact_ = false, downloading_ = false;
  UiTextView text_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer close_timer_{nullptr};
  winrt::Microsoft::UI::Composition::InsetClip clip_{nullptr};
  winrt::Microsoft::UI::Composition::CubicBezierEasingFunction open_easing_{
      nullptr};
  winrt::Microsoft::UI::Composition::LinearEasingFunction linear_{nullptr};
  xaml::UIElement::PointerEntered_revoker button_entered_, list_entered_;
  xaml::UIElement::PointerExited_revoker button_exited_, list_exited_;
  xaml::UIElement::PointerEntered_revoker dropdown_entered_;
  xaml::UIElement::PointerExited_revoker dropdown_exited_;
  xaml::FrameworkElement::SizeChanged_revoker list_sized_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker tick_;
};
}  // namespace kr::win
