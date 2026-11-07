#pragma once

#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include <array>
#include <cstdint>
#include <optional>

#include "theme_pattern.hpp"
#include "view_context.hpp"

namespace kr::win {
// Normalized input is sent to commonMain; this component owns only the
// dotted canvas, pointer capture and native focus/keyboard affordances.
class ThemeCanvas {
 public:
  explicit ThemeCanvas(ViewContext& context);
  ~ThemeCanvas();
  void CancelDrag() noexcept;
  void Render(const WorkspaceTheme& value, float available_width);
  void SetText(UiTextView text);
  [[nodiscard]] const controls::ContentControl& Root() const noexcept {
    return root_;
  }

 private:
  void Arrange();
  void Move(const xaml::Input::PointerRoutedEventArgs& args,
            bool initial = false);
  ViewContext& context_;
  controls::ContentControl root_;
  controls::Grid board_;
  controls::Canvas points_;
  controls::Image pattern_;
  controls::TextBlock hint_{nullptr};
  media::RectangleGeometry clip_;
  std::array<xaml::Shapes::Ellipse, kMaxThemePoints> dots_;
  std::array<media::SolidColorBrush, kMaxThemePoints> colors_;
  ThemePattern pattern_cache_;
  WorkspaceTheme value_;
  float side_ = 0;
  std::optional<std::uint32_t> dragging_pointer_;
  foundation::Point last_pointer_position_{};
  Point drag_offset_;
  xaml::FrameworkElement::SizeChanged_revoker sized_;
  xaml::UIElement::PointerPressed_revoker pressed_;
  xaml::UIElement::PointerMoved_revoker moved_;
  xaml::UIElement::PointerReleased_revoker released_;
  xaml::UIElement::PointerCaptureLost_revoker capture_lost_;
  xaml::UIElement::PointerCanceled_revoker cancelled_;
  xaml::UIElement::KeyDown_revoker key_;
};
}  // namespace kr::win
