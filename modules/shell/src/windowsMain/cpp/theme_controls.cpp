// SPDX-License-Identifier: MPL-2.0
// Wave geometry and dial rules follow ZenGradientGenerator.mjs.
#include "theme_controls.hpp"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>

#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
namespace {
controls::Primitives::Thumb FindHorizontalThumb(
    const xaml::DependencyObject& parent) {
  if (auto thumb = parent.try_as<controls::Primitives::Thumb>();
      thumb && thumb.Name() == L"HorizontalThumb")
    return thumb;
  const int count = media::VisualTreeHelper::GetChildrenCount(parent);
  for (int i = 0; i < count; ++i)
    if (auto thumb =
            FindHorizontalThumb(media::VisualTreeHelper::GetChild(parent, i)))
      return thumb;
  return nullptr;
}

}  // namespace

ThemeControls::ThemeControls(ViewContext& context) : context_(context) {
  const auto& t = kTokens.theme_editor;
  dial_size_ = t.dial_size_rem * kTokens.type.size;
  root_.ColumnSpacing(t.control_gap_rem * kTokens.type.size);
  root_.Margin({t.padding, 0, t.padding, t.row_gap});
  AddColumn(root_, 1, xaml::GridUnitType::Star);
  AddColumn(root_, 0, xaml::GridUnitType::Auto);
  root_.Children().Append(opacity_root_);
  opacity_root_.Height(dial_size_);
  opacity_track_.Height(t.opacity_track_height);
  opacity_track_.CornerRadius(UniformCorners(t.opacity_track_height / 2));
  opacity_track_.Margin(
      {t.opacity_track_inset_start, 0, t.opacity_track_inset_end, 0});
  opacity_track_.VerticalAlignment(xaml::VerticalAlignment::Center);
  opacity_track_.IsHitTestVisible(false);
  opacity_track_.Background(context_.brushes.editor_opacity_track);
  opacity_root_.Children().Append(opacity_track_);
  wave_canvas_.Width(t.wave_width);
  wave_canvas_.Height(t.wave_height);
  wave_canvas_.Children().Append(wave_);
  wave_box_.Child(wave_canvas_);
  wave_box_.Stretch(media::Stretch::Fill);
  wave_box_.IsHitTestVisible(false);
  opacity_root_.Children().Append(wave_box_);
  wave_.StrokeThickness(t.wave_stroke);
  wave_.StrokeStartLineCap(media::PenLineCap::Round);
  wave_.StrokeEndLineCap(media::PenLineCap::Round);
  wave_.Stroke(wave_brush_);
  wave_brush_.StartPoint({0, 0});
  wave_brush_.EndPoint({1, 0});
  for (auto& stop : wave_stops_)
    wave_brush_.GradientStops().Append(stop);
  media::PathGeometry geometry;
  media::PathFigure figure{nullptr};
  std::istringstream data(std::string(t.wave_path));
  curves_.reserve(24);
  char command = 0;
  float start_x = 0;
  while (data >> command) {
    if (command == 'M') {
      float x, y;
      data >> x >> y;
      if (!figure)
        start_x = x;
      figure = media::PathFigure();
      figure.IsClosed(false);
      figure.IsFilled(false);
      figure.StartPoint({x - start_x, t.wave_reference_y - t.wave_origin_y});
      geometry.Figures().Append(figure);
    } else if (command == 'C') {
      Curve curve;
      for (auto& point : curve.source) {
        data >> point.X >> point.Y;
        point.X -= start_x;
      }
      figure.Segments().Append(curve.segment);
      curves_.push_back(std::move(curve));
    } else
      break;
  }
  if (!curves_.empty())
    wave_canvas_.Width(curves_.back().source[2].X);
  wave_.Data(geometry);
  opacity_.Minimum(kTokens.theme.min_opacity);
  opacity_.Maximum(kTokens.theme.max_opacity);
  opacity_.StepFrequency(t.opacity_step);
  opacity_.Margin(UniformThickness(0));
  opacity_.Padding(UniformThickness(0));
  opacity_.MinWidth(0);
  opacity_.MinHeight(0);
  opacity_.Height(dial_size_);
  for (const auto* key :
       {L"SliderTrackFill", L"SliderTrackFillPointerOver",
        L"SliderTrackFillPressed", L"SliderTrackValueFill",
        L"SliderTrackValueFillPointerOver", L"SliderTrackValueFillPressed"})
    opacity_.Resources().Insert(winrt::box_value(key),
                                context_.brushes.transparent);
  for (const auto* key :
       {L"SliderThumbBackground", L"SliderThumbBackgroundPointerOver",
        L"SliderThumbBackgroundPressed", L"SliderThumbBackgroundDisabled"})
    opacity_.Resources().Insert(winrt::box_value(key),
                                context_.brushes.editor_thumb);
  thumb_template_ =
      xaml::Markup::XamlReader::Load(
          LR"(<ControlTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="Thumb"><Border Background="{TemplateBinding Background}" CornerRadius="{TemplateBinding CornerRadius}" /></ControlTemplate>)")
          .as<controls::ControlTemplate>();
  opacity_root_.Children().Append(opacity_);
  loaded_ = opacity_.Loaded(winrt::auto_revoke,
                            [this](auto&&, auto&&) { FindThumb(); });
  opacity_changed_ = opacity_.ValueChanged(
      winrt::auto_revoke,
      [this](auto&&,
             const controls::Primitives::RangeBaseValueChangedEventArgs& args) {
        if (!rendering_)
          context_.delegate.OnThemeEdited(
              {.kind = ThemeEditKind::kOpacity,
               .value = static_cast<float>(args.NewValue())});
      });

  dial_.Width(dial_size_);
  dial_.Height(dial_size_);
  dial_.FlowDirection(xaml::FlowDirection::LeftToRight);
  controls::Grid::SetColumn(dial_, 1);
  root_.Children().Append(dial_);
  dial_.Children().Append(dial_canvas_);
  const float center_size =
      static_cast<float>(dial_size_) * t.dial_inner_fraction;
  for (auto shape : {dial_center_, dial_grain_}) {
    shape.Width(center_size);
    shape.Height(center_size);
    controls::Canvas::SetLeft(shape, (dial_size_ - center_size) / 2);
    controls::Canvas::SetTop(shape, (dial_size_ - center_size) / 2);
    dial_canvas_.Children().Append(shape);
  }
  media::LinearGradientBrush center;
  center.StartPoint({0, 1});
  center.EndPoint({1, 0});
  media::GradientStop first;
  first.Offset(0);
  first.Color(NativeColor(Color{}));
  center_end_.Offset(1);
  center_end_.Color(context_.brushes.editor_hover.Color());
  center.GradientStops().Append(first);
  center.GradientStops().Append(center_end_);
  dial_center_.Fill(center);
  dial_center_.Stroke(context_.brushes.separator);
  dial_center_.StrokeThickness(t.border_width);
  dial_grain_brush_.ImageSource(
      grain_.Grain(static_cast<int>(std::ceil(center_size)),
                   static_cast<int>(std::ceil(center_size)), {}));
  dial_grain_brush_.Stretch(media::Stretch::Fill);
  dial_grain_.Fill(dial_grain_brush_);
  for (auto& tick : ticks_) {
    tick.Width(t.dial_dot_size);
    tick.Height(t.dial_dot_size);
    tick.Fill(context_.brushes.editor_dial_dots);
    tick.IsHitTestVisible(false);
    dial_canvas_.Children().Append(tick);
  }
  dial_handler_.Width(t.dial_handler_width);
  dial_handler_.Height(t.dial_handler_height);
  dial_handler_.RadiusX(t.dial_handler_radius);
  dial_handler_.RadiusY(t.dial_handler_radius);
  dial_handler_.Fill(context_.brushes.editor_dial_handler);
  dial_handler_.RenderTransform(dial_rotation_);
  dial_rotation_.CenterX(t.dial_handler_width / 2);
  dial_rotation_.CenterY(t.dial_handler_height / 2);
  dial_canvas_.Children().Append(dial_handler_);
  texture_.Minimum(0);
  texture_.Maximum(static_cast<double>(kThemeTextureSteps - 1) /
                   kThemeTextureSteps);
  texture_.StepFrequency(1.0 / kThemeTextureSteps);
  texture_.Opacity(0);
  texture_.IsHitTestVisible(false);
  dial_.Children().Append(texture_);
  texture_changed_ = texture_.ValueChanged(
      winrt::auto_revoke,
      [this](auto&&,
             const controls::Primitives::RangeBaseValueChangedEventArgs& args) {
        if (!rendering_)
          context_.delegate.OnThemeEdited(
              {.kind = ThemeEditKind::kTexture,
               .value = static_cast<float>(args.NewValue())});
      });
  pressed_ = dial_.PointerPressed(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (!args.GetCurrentPoint(dial_).Properties().IsLeftButtonPressed())
          return;
        texture_.Focus(xaml::FocusState::Pointer);
        dragging_ = dial_.CapturePointer(args.Pointer());
        SetTexture(args);
        args.Handled(true);
      });
  moved_ = dial_.PointerMoved(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (dragging_) {
          SetTexture(args);
          args.Handled(true);
        }
      });
  released_ = dial_.PointerReleased(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (dragging_) {
          SetTexture(args);
          dragging_ = false;
          dial_.ReleasePointerCapture(args.Pointer());
          args.Handled(true);
        }
      });
  lost_ = dial_.PointerCaptureLost(
      winrt::auto_revoke, [this](auto&&, auto&&) { dragging_ = false; });
  focused_ = texture_.GotFocus(winrt::auto_revoke, [this](auto&&, auto&&) {
    dial_center_.Stroke(context_.brushes.editor_foreground);
  });
  blurred_ = texture_.LostFocus(winrt::auto_revoke, [this](auto&&, auto&&) {
    dial_center_.Stroke(context_.brushes.separator);
  });
}

void ThemeControls::FindThumb() noexcept try {
  thumb_ = FindHorizontalThumb(opacity_);
  if (!thumb_)
    return;
  // Keep WinUI's complete Slider template and native input machinery. Its
  // fixed/auto tick rows don't fill the wave's height, even when the thumb
  // itself is centered. Normalize only the horizontal layout's rows.
  const auto slider = opacity_.as<xaml::DependencyObject>();
  for (auto parent = media::VisualTreeHelper::GetParent(thumb_);
       parent && parent != slider;
       parent = media::VisualTreeHelper::GetParent(parent)) {
    const auto grid = parent.try_as<controls::Grid>();
    if (!grid)
      continue;
    const auto name = grid.Name();
    if (name != L"HorizontalTemplate" && name != L"SliderContainer")
      continue;
    grid.Height(dial_size_);
    grid.MinHeight(0);
    grid.Margin(UniformThickness(0));
    grid.Padding(UniformThickness(0));
    grid.VerticalAlignment(xaml::VerticalAlignment::Center);
    if (name == L"HorizontalTemplate") {
      for (const auto& child : grid.Children()) {
        if (const auto element = child.try_as<xaml::FrameworkElement>()) {
          controls::Grid::SetRow(element, 0);
          controls::Grid::SetRowSpan(element, 1);
        }
      }
      grid.RowDefinitions().Clear();
      AddRow(grid, 1, xaml::GridUnitType::Star);
    }
  }
  thumb_.VerticalAlignment(xaml::VerticalAlignment::Center);
  thumb_.MinWidth(0);
  thumb_.MinHeight(0);
  thumb_.Template(thumb_template_);
  thumb_.Background(context_.brushes.editor_thumb);
  const double p = (opacity_.Value() - kTokens.theme.min_opacity) /
                   (kTokens.theme.max_opacity - kTokens.theme.min_opacity);
  thumb_.Width(kTokens.theme_editor.thumb_width +
               kTokens.theme_editor.thumb_growth * p);
  thumb_.Height(kTokens.theme_editor.thumb_height +
                kTokens.theme_editor.thumb_growth * p);
  thumb_.CornerRadius(UniformCorners(thumb_.Width() / 2));
} catch (...) {
  thumb_ = nullptr;
  platform::WriteDefaultLog(
      LogLevel::kWarning,
      "Opacity slider styling could not be applied; keeping native input.");
}

void ThemeControls::SetTexture(
    const xaml::Input::PointerRoutedEventArgs& args) {
  const auto p = args.GetCurrentPoint(dial_).Position();
  const float tau = 2 * std::numbers::pi_v<float>;
  float value = (std::atan2(p.Y - static_cast<float>(dial_size_ / 2),
                            p.X - static_cast<float>(dial_size_ / 2)) +
                 tau / 4) /
                tau;
  value -= std::floor(value);
  context_.delegate.OnThemeEdited(
      {.kind = ThemeEditKind::kTexture, .value = value});
}

void ThemeControls::PositionDial() {
  const auto& t = kTokens.theme_editor;
  const double tau = 2 * std::numbers::pi;
  for (std::size_t i = 0; i < ticks_.size(); ++i) {
    const double angle = static_cast<double>(i) / ticks_.size() * tau - tau / 4;
    controls::Canvas::SetLeft(
        ticks_[i],
        dial_size_ / 2 * (1 + std::cos(angle)) - t.dial_dot_size / 2);
    controls::Canvas::SetTop(ticks_[i], dial_size_ / 2 * (1 + std::sin(angle)) -
                                            t.dial_dot_size / 2);
    ticks_[i].Opacity(static_cast<float>(i) < texture_value_ * ticks_.size()
                          ? 1
                          : t.dial_inactive_opacity);
  }
  const double angle = texture_value_ * tau - tau / 4;
  controls::Canvas::SetLeft(
      dial_handler_,
      dial_size_ / 2 * (1 + std::cos(angle)) - t.dial_handler_width / 2);
  controls::Canvas::SetTop(
      dial_handler_,
      dial_size_ / 2 * (1 + std::sin(angle)) - t.dial_handler_height / 2);
  dial_rotation_.Angle(texture_value_ * 360);
}

void ThemeControls::Render(const WorkspaceTheme& theme, bool stacked) {
  if (stacked_ != stacked) {
    stacked_ = stacked;
    root_.ColumnDefinitions().Clear();
    root_.RowDefinitions().Clear();
    AddColumn(root_, 1, xaml::GridUnitType::Star);
    AddRow(root_, 0, xaml::GridUnitType::Auto);
    if (stacked)
      AddRow(root_, 0, xaml::GridUnitType::Auto);
    else
      AddColumn(root_, 0, xaml::GridUnitType::Auto);
    controls::Grid::SetColumn(dial_, stacked ? 0 : 1);
    controls::Grid::SetRow(dial_, stacked ? 1 : 0);
    root_.RowSpacing(kTokens.theme_editor.row_gap);
    dial_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  }
  rendering_ = true;
  const auto& t = kTokens.theme_editor;
  opacity_.IsEnabled(theme.point_count > 0);
  opacity_.Visibility(theme.point_count ? xaml::Visibility::Visible
                                        : xaml::Visibility::Collapsed);
  opacity_.Value(theme.opacity);
  texture_.Value(theme.texture);
  texture_value_ = theme.texture;
  const float progress =
      theme.point_count
          ? (theme.opacity - kTokens.theme.min_opacity) /
                (kTokens.theme.max_opacity - kTokens.theme.min_opacity)
          : 1;
  for (auto& curve : curves_) {
    const auto at = [&](std::size_t i) {
      return foundation::Point{
          curve.source[i].X,
          t.wave_reference_y +
              (curve.source[i].Y - t.wave_reference_y) * progress -
              t.wave_origin_y};
    };
    curve.segment.Point1(at(0));
    curve.segment.Point2(at(1));
    curve.segment.Point3(at(2));
  }
  wave_stops_[0].Offset(0);
  wave_stops_[0].Color(context_.brushes.editor_wave.Color());
  wave_stops_[1].Offset(progress);
  wave_stops_[1].Color(context_.brushes.editor_wave.Color());
  wave_stops_[2].Offset(progress);
  wave_stops_[2].Color(context_.brushes.editor_wave_muted.Color());
  if (thumb_) {
    thumb_.Width(t.thumb_width + t.thumb_growth * progress);
    thumb_.Height(t.thumb_height + t.thumb_growth * progress);
    thumb_.CornerRadius(UniformCorners(thumb_.Width() / 2));
  }
  dial_grain_.Opacity(theme.texture);
  center_end_.Color(context_.brushes.editor_hover.Color());
  PositionDial();
  rendering_ = false;
}

void ThemeControls::SetText(UiTextView text) {
  Accessible(opacity_, NativeText(UiText::kThemeOpacity, text));
  Accessible(texture_, NativeText(UiText::kThemeTexture, text));
  wave_brush_.StartPoint(text.right_to_left ? foundation::Point{1, 0}
                                            : foundation::Point{0, 0});
  wave_brush_.EndPoint(text.right_to_left ? foundation::Point{0, 0}
                                          : foundation::Point{1, 0});
}
}  // namespace kr::win
