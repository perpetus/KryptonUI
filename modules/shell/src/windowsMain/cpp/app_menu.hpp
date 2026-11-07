#pragma once

#include "menu_material.hpp"
#include "transient_surface.hpp"
#include "view_context.hpp"

namespace kr::win {
// A native multiview panel for account headers, ordinary menu rows and inline
// controls. Every label, command, icon and enabled state comes from the host.
class AppMenuPresenter {
 public:
  AppMenuPresenter(HWND parent, ViewContext& context, Task on_closed);
  ~AppMenuPresenter();
  void Close(bool release = true) noexcept;
  void SetBackText(winrt::hstring text) { back_text_ = std::move(text); }
  void Show(std::uint32_t request,
            const MenuTree& tree,
            Rect anchor,
            const ImageStore& images,
            Rect viewport,
            float scale,
            xaml::ElementTheme theme,
            xaml::FlowDirection direction);
  void Update(Rect viewport,
              float scale,
              xaml::ElementTheme theme,
              xaml::FlowDirection direction);
  void Reanchor(Rect anchor);
  void Raise() noexcept { island_.Raise(); }
  [[nodiscard]] bool IsOpen() const noexcept { return open_; }
  [[nodiscard]] bool Contains(Point point) const noexcept {
    return open_ && frame_.Contains(point);
  }
  [[nodiscard]] std::uint32_t Request() const noexcept { return request_; }

 private:
  void Build(std::size_t index);
  void Navigate(std::size_t index, winrt::hstring title) noexcept;
  void Back() noexcept;
  void Focus(const controls::Button& button);
  controls::Button Command(const MenuItem& item,
                           bool compact = false,
                           bool show_shortcut = true);
  void Row(const MenuItem& item);
  void Controls(const MenuItem& item);
  void Tiles(const MenuItem& item);
  void Section(const MenuItem& item);
  void Footer(const MenuItem& item);
  void Separator();
  void FocusFirst();
  [[nodiscard]] bool RefreshInPlace(const MenuTree& tree);
  void Key(const xaml::Input::KeyRoutedEventArgs& args);
  void EnsureOwner();
  void Reposition() noexcept;
  void OpenWhenLoaded() noexcept;

  ViewContext& context_;
  ContentFade fade_;
  MenuMaterial material_;
  Task on_closed_;
  TransientSurface island_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  MenuTree tree_;
  std::vector<std::size_t> path_;
  std::vector<winrt::hstring> titles_;
  std::vector<std::unique_ptr<NativeButton>> buttons_;
  std::vector<controls::Button> focus_targets_;
  std::unordered_map<ImageId, media::ImageSource> menu_images_;
  controls::Primitives::Popup popup_;
  controls::Grid layers_;
  controls::SystemBackdropElement backdrop_;
  controls::Border surface_;
  controls::ScrollViewer scroll_;
  controls::StackPanel content_;
  std::uint32_t request_ = 0;
  std::uint64_t generation_ = 0;
  bool open_ = false;
  bool tiled_ = false;
  bool opened_ = false, attached_ = false, positioning_ = false;
  Rect anchor_;
  Rect frame_;
  winrt::hstring back_text_ = NativeText(UiText::kBack);
  controls::Primitives::Popup::Closed_revoker closed_;
  controls::Primitives::Popup::Opened_revoker popup_opened_;
  xaml::UIElement::KeyDown_revoker key_;
  xaml::UIElement::ProcessKeyboardAccelerators_revoker fallback_key_;
  xaml::FrameworkElement::Loaded_revoker focus_loaded_;
  xaml::FrameworkElement::Loaded_revoker owner_loaded_;
  xaml::FrameworkElement::SizeChanged_revoker owner_sized_, surface_sized_;
};
}  // namespace kr::win
