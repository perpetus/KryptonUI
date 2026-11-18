#pragma once

#include <array>
#include <memory>

#include "menu_material.hpp"
#include "transient_surface.hpp"
#include "view_context.hpp"

namespace kr::win {
class TabSwitcher {
 public:
  TabSwitcher(HWND parent,
              ViewContext& context,
              std::move_only_function<void()> closed)
      : context_(context),
        island_(parent),
        closed_callback_(std::move(closed)) {}
  ~TabSwitcher();
  void Render(const ShellViewModel& vm, xaml::ElementTheme theme);
  void Close() noexcept;
  void Raise() noexcept { island_.Raise(); }
  [[nodiscard]] bool OwnsInput() const noexcept { return open_ || closing_; }
  [[nodiscard]] int Columns() const noexcept { return columns_; }

 private:
  struct Card {
    TabId id;
    controls::Button button{nullptr};
    controls::Image favicon{nullptr}, preview{nullptr};
    controls::TextBlock title{nullptr}, placeholder{nullptr};
    controls::Grid image{nullptr};
    controls::Button::Click_revoker click;
  };
  void Ensure();
  void OpenWhenLoaded();
  ViewContext& context_;
  TransientSurface island_;
  std::move_only_function<void()> closed_callback_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  controls::Primitives::Popup popup_{nullptr};
  controls::Grid overlay_{nullptr};
  controls::Border surface_{nullptr};
  controls::Grid cards_{nullptr};
  controls::TextBlock heading_{nullptr}, hint_{nullptr};
  std::array<Card, kTabSwitcherPageSize> cards;
  xaml::Media::SystemBackdrop backdrop_{nullptr};
  xaml::FrameworkElement::Loaded_revoker loaded_;
  controls::Primitives::Popup::Closed_revoker closed_;
  controls::Primitives::Popup::Opened_revoker opened_;
  xaml::UIElement::Tapped_revoker background_tapped_;
  bool open_ = false, closing_ = false, focus_pending_ = false;
  std::uint64_t generation_ = 0;
  std::size_t selected_ = 0, first_ = 0;
  int columns_ = 3;
  Rect bounds_;
};
}  // namespace kr::win
