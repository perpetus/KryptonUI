#pragma once

#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include <array>
#include <vector>

#include "theme_pattern.hpp"
#include "view_context.hpp"

namespace kr::win {

class ThemeControls {
 public:
  explicit ThemeControls(ViewContext& context);
  void Render(const WorkspaceTheme& theme, bool stacked);
  void SetText(UiTextView text);
  [[nodiscard]] const controls::Grid& Root() const noexcept { return root_; }

 private:
  void PositionDial();
  void SetTexture(const xaml::Input::PointerRoutedEventArgs& args);
  void FindThumb() noexcept;
  ViewContext& context_;
  controls::Grid root_, opacity_root_, dial_;
  controls::Border opacity_track_;
  controls::Canvas dial_canvas_;
  controls::Slider opacity_, texture_;
  controls::Primitives::Thumb thumb_{nullptr};
  controls::ControlTemplate thumb_template_{nullptr};
  controls::Viewbox wave_box_;
  controls::Canvas wave_canvas_;
  xaml::Shapes::Path wave_;
  media::LinearGradientBrush wave_brush_;
  media::GradientStop center_end_;
  std::array<media::GradientStop, 3> wave_stops_;
  struct Curve {
    media::BezierSegment segment;
    std::array<foundation::Point, 3> source;
  };
  std::vector<Curve> curves_;
  std::array<xaml::Shapes::Ellipse, kThemeTextureSteps> ticks_;
  xaml::Shapes::Ellipse dial_center_, dial_grain_;
  xaml::Shapes::Rectangle dial_handler_;
  media::RotateTransform dial_rotation_;
  media::ImageBrush dial_grain_brush_;
  ThemePattern grain_;
  float texture_value_ = 0;
  double dial_size_ = 0;
  bool rendering_ = false, dragging_ = false;
  bool stacked_ = false;
  controls::Slider::ValueChanged_revoker opacity_changed_, texture_changed_;
  xaml::FrameworkElement::Loaded_revoker loaded_;
  xaml::UIElement::PointerPressed_revoker pressed_;
  xaml::UIElement::PointerMoved_revoker moved_;
  xaml::UIElement::PointerReleased_revoker released_;
  xaml::UIElement::PointerCaptureLost_revoker lost_;
  xaml::UIElement::GotFocus_revoker focused_;
  xaml::UIElement::LostFocus_revoker blurred_;
};
}  // namespace kr::win
