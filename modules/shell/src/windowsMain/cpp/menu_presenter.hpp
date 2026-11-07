#pragma once

#include <windows.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "menu_material.hpp"
#include "view_context.hpp"

namespace kr::win {
class AppMenuPresenter;

class MenuPresenter {
 public:
  MenuPresenter(HWND parent, ViewContext& context, Task on_closed);
  ~MenuPresenter();
  void Close(bool release = true) noexcept;
  void Show(std::uint32_t request,
            const MenuTree& tree,
            Rect anchor,
            const ImageStore& images,
            MenuKind kind,
            const ShellLayout& layout,
            const controls::Grid& sidebar,
            const controls::Grid& toolbar,
            Rect viewport,
            float scale);
  void Update(Rect viewport,
              float scale,
              xaml::ElementTheme theme,
              xaml::FlowDirection direction);
  void Reanchor(Rect anchor);
  void Raise() noexcept;
  [[nodiscard]] bool IsOpen() const noexcept;
  [[nodiscard]] bool Contains(Point point) const noexcept;
  void SetText(UiTextView text);
  void InstallStyle(const xaml::FrameworkElement& root);

 private:
  struct NativeItem;
  void RefreshGlassMenus() noexcept;
  void ClearGlassMenus() noexcept;
  [[nodiscard]] xaml::Style PresenterStyle(xaml::ElementTheme theme);
  void StyleItem(const controls::Control& item, bool submenu);
  void ShowMenu(std::uint32_t request_id,
                const MenuTree& tree,
                Rect anchor,
                const ImageStore& images,
                MenuKind kind,
                const ShellLayout& layout,
                const controls::Grid& sidebar,
                const controls::Grid& toolbar);
  void RefreshMenu(std::uint32_t request_id,
                   const MenuTree& tree,
                   const ImageStore& images);
  void UpdateMenu(
      const MenuTree& tree,
      std::size_t index,
      const foundation::Collections::IVector<controls::MenuFlyoutItemBase>&
          output,
      std::vector<std::unique_ptr<NativeItem>>& rows,
      std::uint32_t request_id,
      std::size_t depth = 0);
  [[nodiscard]] std::unique_ptr<NativeItem> CreateItem(
      const MenuItem& item,
      std::uint32_t request_id);
  void SetSubmenu(const controls::MenuFlyoutSubItem& submenu,
                  const MenuItem& item);
  void SetItem(const controls::MenuFlyoutItem& native, const MenuItem& item);
  void UpdateIcon(const controls::MenuFlyoutItem& native, const MenuItem& item);
  void UpdateIcon(const controls::MenuFlyoutSubItem& native,
                  const MenuItem& item);
  media::ImageSource Bitmap(ImageId id) {
    return context_.images.Get(id, images_);
  }
  ViewContext& context_;
  MenuMaterial material_;
  HWND parent_;
  Task on_closed_;
  const ImageStore* images_ = nullptr;
  controls::MenuFlyout menu_{nullptr};
  xaml::FrameworkElement menu_owner_{nullptr};
  Rect owner_bounds_;
  std::vector<std::unique_ptr<NativeItem>> menu_rows_;
  controls::Primitives::FlyoutBase::Closed_revoker menu_closed_;
  controls::Primitives::FlyoutBase::Opened_revoker menu_opened_;
  xaml::FrameworkElement::LayoutUpdated_revoker menu_glass_layout_;
  std::vector<controls::MenuFlyoutPresenter> glass_menus_, seen_glass_menus_;
  bool refreshing_glass_ = false;
  bool menu_open_ = false;
  std::uint32_t menu_request_ = 0;
  std::uint64_t menu_generation_ = 0;
  MenuKind kind_ = MenuKind::kApp;
  std::array<xaml::Style, 3> presenter_styles_{nullptr, nullptr, nullptr};
  std::array<controls::ControlTemplate, 3> item_templates_{nullptr, nullptr,
                                                           nullptr};
  std::array<bool, 3> item_template_failed_{};
  std::unique_ptr<AppMenuPresenter> app_menu_;
  winrt::hstring back_text_ = NativeText(UiText::kBack);
};

}  // namespace kr::win
