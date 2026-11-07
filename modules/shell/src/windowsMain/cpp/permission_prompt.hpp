#pragma once

#include "menu_material.hpp"
#include "transient_surface.hpp"
#include "view_context.hpp"

namespace kr::win {
class PermissionPromptView {
 public:
  PermissionPromptView(HWND parent, ViewContext& context);
  ~PermissionPromptView();
  void Render(const ShellViewModel& vm);

 private:
  void Build();
  void Close() noexcept;
  void PositionAndOpen() noexcept;
  void Decide(PermissionDecision value);
  ViewContext& context_;
  TransientSurface island_;
  MenuMaterial material_;
  xaml::Style accent_{nullptr};
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  controls::Primitives::Popup popup_;
  controls::Grid layers_, content_;
  controls::SystemBackdropElement backdrop_;
  controls::FlyoutPresenter surface_;
  controls::ScrollViewer scroll_;
  controls::TextBlock origin_, title_, embedding_, message_;
  controls::StackPanel requests_, actions_;
  controls::StackPanel embedding_section_;
  std::vector<controls::Grid> request_rows_;
  std::vector<controls::TextBlock> request_icons_, request_labels_;
  std::unique_ptr<NativeButton> allow_once_, allow_always_, block_, close_;
  std::uint64_t request_ = 0, revision_ = ~std::uint64_t{0};
  Rect anchor_, client_;
  bool built_ = false, open_ = false, positioning_ = false;
  controls::Primitives::Popup::Closed_revoker closed_;
  controls::Primitives::Popup::Opened_revoker opened_;
  xaml::FrameworkElement::Loaded_revoker loaded_;
  xaml::FrameworkElement::SizeChanged_revoker sized_;
  xaml::UIElement::KeyDown_revoker key_;
  xaml::UIElement::ProcessKeyboardAccelerators_revoker fallback_key_;
};
}  // namespace kr::win
