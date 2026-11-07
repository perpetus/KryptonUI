// SPDX-License-Identifier: MPL-2.0
// Layer ordering/stops follow ZenGradientGenerator.mjs::getGradient.
#include "theme_backdrop.hpp"

#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <cmath>

namespace kr::win {
namespace {
void Stops(const std::array<media::GradientStop, 4>& stops,
           const ThemeGradientLayer& layer) {
  const std::array offsets{0.0f, std::clamp(layer.start, 0.0f, 1.0f),
                           std::clamp(layer.end, 0.0f, 1.0f), 1.0f};
  for (std::size_t i = 0; i < stops.size(); ++i) {
    stops[i].Offset(offsets[i]);
    stops[i].Color(NativeColor(layer.color.WithAlpha(static_cast<std::uint8_t>(
        std::lround(GradientOpacity(layer, offsets[i]) * 255)))));
  }
}
}  // namespace

ThemeBackdrop::ThemeBackdrop() {
  root_.IsHitTestVisible(false);
  root_.FlowDirection(xaml::FlowDirection::LeftToRight);
  root_.Children().Append(gradient_layers_);
  root_.Children().Append(smooth_);
}

void ThemeBackdrop::EnsureLayer(std::size_t index, bool radial) {
  auto& layer = layers_[index];
  if (!layer.element) {
    layer.element = controls::Border();
    gradient_layers_.Children().Append(layer.element);
  }
  if (radial && !layer.radial) {
    layer.radial = media::RadialGradientBrush();
    layer.radial.MappingMode(media::BrushMappingMode::Absolute);
    for (auto& stop : layer.radial_stops) {
      stop = media::GradientStop();
      layer.radial.GradientStops().Append(stop);
    }
  } else if (!radial && !layer.linear) {
    layer.linear = media::LinearGradientBrush();
    layer.linear.MappingMode(media::BrushMappingMode::Absolute);
    for (auto& stop : layer.line_stops) {
      stop = media::GradientStop();
      layer.linear.GradientStops().Append(stop);
    }
  }
  layer.element.Visibility(xaml::Visibility::Visible);
}

void ThemeBackdrop::Linear(std::size_t index, const ThemeGradientLayer& value) {
  EnsureLayer(index, false);
  auto& layer = layers_[index];
  const auto line = GradientLine(value, client_);
  layer.linear.StartPoint({line.start.x - region_.x, line.start.y - region_.y});
  layer.linear.EndPoint({line.end.x - region_.x, line.end.y - region_.y});
  Stops(layer.line_stops, value);
  layer.element.Background(layer.linear);
}

void ThemeBackdrop::Radial(std::size_t index, const ThemeGradientLayer& value) {
  EnsureLayer(index, true);
  auto& layer = layers_[index];
  const auto circle = GradientCircle(value, client_);
  const foundation::Point center{circle.center.x - region_.x,
                                 circle.center.y - region_.y};
  layer.radial.Center(center);
  layer.radial.GradientOrigin(center);
  layer.radial.RadiusX(circle.radius);
  layer.radial.RadiusY(circle.radius);
  Stops(layer.radial_stops, value);
  layer.element.Background(layer.radial);
}

void ThemeBackdrop::Render(const ThemeVm& theme,
                           Rect client,
                           Rect region,
                           bool high_contrast) {
  auto gradient =
      MakeLinearThemeGradient(theme.gradient_start, theme.gradient_end, client);
  float texture = 0;
  if (theme.workspace_theme) {
    const auto& value = *theme.workspace_theme;
    std::array<Color, kMaxThemePoints> colors;
    for (std::size_t i = 0; i < value.point_count; ++i)
      colors[i] = value.points[i].color;
    gradient = MakeThemeGradient(Color{theme.palette->gradient_base},
                                 std::span{colors}.first(value.point_count),
                                 value.opacity);
    texture = value.texture;
  }
  Render(gradient, texture, client, region, high_contrast);
}

void ThemeBackdrop::Render(const ThemeGradient& gradient,
                           float texture,
                           Rect client,
                           Rect region,
                           bool high_contrast) {
  if (high_contrast || region.empty() || client.empty()) {
    root_.Visibility(xaml::Visibility::Collapsed);
    return;
  }
  root_.Visibility(xaml::Visibility::Visible);
  float scale = 1;
  if (const auto xaml_root = root_.XamlRoot())
    scale = static_cast<float>(xaml_root.RasterizationScale());
  if (last_ && *last_ == gradient && texture_ == texture && region_ == region &&
      client_ == client && scale_ == scale)
    return;
  last_ = gradient;
  client_ = client;
  region_ = region;
  texture_ = texture;
  scale_ = scale;
  base_.Color(NativeColor(gradient.base));
  root_.Background(base_);
  gradient_layers_.Visibility(xaml::Visibility::Visible);
  for (auto& layer : layers_)
    if (layer.element)
      layer.element.Visibility(xaml::Visibility::Collapsed);
  for (std::size_t i = 0; i < gradient.layer_count; ++i)
    if (gradient.layers[i].kind == ThemeGradientKind::kRadial)
      Radial(i, gradient.layers[i]);
    else
      Linear(i, gradient.layers[i]);
  smooth_.Visibility(xaml::Visibility::Collapsed);
  if (gradient.layer_count && !smoothing_unavailable_) {
    try {
      if (!renderer_) {
        renderer_ =
            std::make_unique<ThemeGradientRenderer>(root_, gradient_layers_);
        smooth_.Background(renderer_->Brush());
      }
      renderer_->Render(gradient, client, region, scale);
      smooth_.Visibility(xaml::Visibility::Visible);
    } catch (...) {
      // A graphics initialization failure must never hide the native theme.
      smoothing_unavailable_ = true;
      renderer_.reset();
      smooth_.Background(nullptr);
    }
  }
  if (texture > 0 && !grain_image_) {
    grain_image_ = controls::Image();
    grain_image_.IsHitTestVisible(false);
    grain_image_.Stretch(media::Stretch::Fill);
    root_.Children().Append(grain_image_);
  }
  if (grain_image_) {
    if (texture > 0) {
      const auto bitmap =
          grain_.Grain(static_cast<int>(std::ceil(region.width)),
                       static_cast<int>(std::ceil(region.height)),
                       {region.x - client.x, region.y - client.y});
      if (grain_image_.Source() != bitmap)
        grain_image_.Source(bitmap);
    }
    grain_image_.Opacity(texture);
  }
}
}  // namespace kr::win
