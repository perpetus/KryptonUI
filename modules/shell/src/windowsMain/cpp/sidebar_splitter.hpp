#pragma once

#include "view_context.hpp"

namespace kr::win {

class SidebarSplitter {
 public:
  explicit SidebarSplitter(ViewContext& context);
  void SetLayout(const ShellLayout& layout, float scale);
  void SetText(UiTextView text);
  [[nodiscard]] const controls::Border& Root() const noexcept { return root_; }

 private:
  void Finish();
  ViewContext& context_;
  controls::Border root_;
  ShellLayout layout_;
  float scale_ = 1;
  float start_width_ = 0;
  long start_x_ = 0;
  bool dragging_ = false;
  bool right_ = false;
  xaml::UIElement::PointerPressed_revoker pressed_;
  xaml::UIElement::PointerMoved_revoker moved_;
  xaml::UIElement::PointerReleased_revoker released_;
  xaml::UIElement::PointerCaptureLost_revoker capture_lost_;
  xaml::UIElement::DoubleTapped_revoker double_tapped_;
};

}  // namespace kr::win
