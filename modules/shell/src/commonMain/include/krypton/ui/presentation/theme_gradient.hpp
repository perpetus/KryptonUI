// SPDX-License-Identifier: MPL-2.0
// Layer ordering and stops follow ZenGradientGenerator.mjs::getGradient.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>

#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/design/tokens.hpp"

namespace kr {
enum class ThemeGradientKind { kLinear, kRadial };

struct ThemeGradientLayer {
  ThemeGradientKind kind = ThemeGradientKind::kLinear;
  Color color;
  float angle = 0;
  float center_x = 0;
  float start = 0;
  float end = 1;
  friend bool operator==(const ThemeGradientLayer&,
                         const ThemeGradientLayer&) = default;
};

struct ThemeGradient {
  Color base;
  std::array<ThemeGradientLayer, 3> layers{};
  std::size_t layer_count = 0;
  friend bool operator==(const ThemeGradient&, const ThemeGradient&) = default;
};

// Match a native top-left to bottom-right brush in full-client coordinates.
[[nodiscard]] inline ThemeGradient
MakeLinearThemeGradient(Color start, Color end, Rect client) noexcept {
  ThemeGradient result{.base = end};
  if (start != end && !client.empty()) {
    result.layer_count = 1;
    result.layers[0] = {ThemeGradientKind::kLinear, start,
                        std::atan2(client.width, -client.height) * 180 /
                            std::numbers::pi_v<float>};
  }
  return result;
}

// Header-only so a C-ABI host can use the same rules without linking the shell.
[[nodiscard]] inline ThemeGradient MakeThemeGradient(
    Color base,
    std::span<const Color> colors,
    float opacity) noexcept {
  ThemeGradient result{.base = base};
  const auto color = [&](std::size_t i) {
    return Mix(base, colors[i], opacity);
  };
  const auto& t = design::kTokens.workspace_gradient;
  if (colors.size() == 1) {
    result.base = color(0);
  } else if (colors.size() == 2) {
    result.layer_count = 2;
    result.layers[0] = {ThemeGradientKind::kLinear, color(1),
                        t.two_color.angle,          0,
                        t.two_color.start,          t.two_color.end};
    result.layers[1] = {ThemeGradientKind::kLinear, color(0),
                        t.two_color.angle + 180,    0,
                        t.two_color.start,          t.two_color.end};
  } else if (colors.size() == 3) {
    result.layer_count = 3;
    result.layers[0] = {ThemeGradientKind::kRadial,
                        color(0),
                        0,
                        t.three_color_left.center_x,
                        t.three_color_left.start,
                        t.three_color_left.end};
    result.layers[1] = {ThemeGradientKind::kRadial,
                        color(1),
                        0,
                        t.three_color_right.center_x,
                        t.three_color_right.start,
                        t.three_color_right.end};
    result.layers[2] = {ThemeGradientKind::kLinear, color(2),
                        t.three_color_linear.angle, 0,
                        t.three_color_linear.start, t.three_color_linear.end};
  }
  return result;
}

struct ThemeGradientLine {
  Point start, end;
};
[[nodiscard]] inline ThemeGradientLine GradientLine(
    const ThemeGradientLayer& layer,
    Rect client) noexcept {
  const float angle = layer.angle * std::numbers::pi_v<float> / 180;
  const float dx = std::sin(angle), dy = -std::cos(angle);
  const float half =
      (std::abs(dx) * client.width + std::abs(dy) * client.height) / 2;
  const Point center{client.x + client.width / 2, client.y + client.height / 2};
  return {{center.x - dx * half, center.y - dy * half},
          {center.x + dx * half, center.y + dy * half}};
}

struct ThemeGradientCircle {
  Point center;
  float radius;
};
[[nodiscard]] inline ThemeGradientCircle GradientCircle(
    const ThemeGradientLayer& layer,
    Rect client) noexcept {
  return {
      {client.x + layer.center_x * client.width, client.y},
      std::hypot(std::max(layer.center_x, 1 - layer.center_x) * client.width,
                 client.height)};
}

[[nodiscard]] inline float GradientOpacity(const ThemeGradientLayer& layer,
                                           float at) noexcept {
  return 1 -
         std::clamp((at - layer.start) / (layer.end - layer.start), 0.0f, 1.0f);
}

[[nodiscard]] inline Color SampleThemeGradient(const ThemeGradient& gradient,
                                               Rect client,
                                               Point point) noexcept {
  auto result = gradient.base;
  if (client.empty())
    return result;
  for (std::size_t i = 0; i < gradient.layer_count; ++i) {
    const auto& layer = gradient.layers[i];
    float at = 0;
    if (layer.kind == ThemeGradientKind::kRadial) {
      const auto circle = GradientCircle(layer, client);
      at = std::hypot(point.x - circle.center.x, point.y - circle.center.y) /
           circle.radius;
    } else {
      const auto line = GradientLine(layer, client);
      const float dx = line.end.x - line.start.x,
                  dy = line.end.y - line.start.y;
      at = ((point.x - line.start.x) * dx + (point.y - line.start.y) * dy) /
           (dx * dx + dy * dy);
    }
    result = Mix(result, layer.color, GradientOpacity(layer, at));
  }
  return result;
}
}  // namespace kr
