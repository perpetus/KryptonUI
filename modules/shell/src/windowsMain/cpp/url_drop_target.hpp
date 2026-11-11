#pragma once

#include <winrt/Windows.ApplicationModel.DataTransfer.h>

#include <memory>

#include "view_context.hpp"

namespace kr::win {
// One per steady island. External URL drops do not participate in tab/folder
// reordering and do not programmatically focus a XAML control.
class UrlDropTarget {
 public:
  UrlDropTarget(ViewContext& context, const xaml::FrameworkElement& root);
  ~UrlDropTarget();
  [[nodiscard]] bool IsReadingDrop() const noexcept { return drop_pending_; }

 private:
  static bool Supports(const xaml::DragEventArgs& args);
  void Update(const xaml::DragEventArgs& args, bool active);
  winrt::fire_and_forget Drop(xaml::DragEventArgs args);

  ViewContext& context_;
  xaml::FrameworkElement root_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  std::uint64_t generation_ = 0;
  bool drop_pending_ = false;
  xaml::UIElement::DragEnter_revoker entered_;
  xaml::UIElement::DragOver_revoker over_;
  xaml::UIElement::DragLeave_revoker left_;
  xaml::UIElement::Drop_revoker dropped_;
};
}  // namespace kr::win
