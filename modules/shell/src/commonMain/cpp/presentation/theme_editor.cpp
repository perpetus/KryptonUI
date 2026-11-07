// SPDX-License-Identifier: MPL-2.0
// Color geometry and harmonies adapted from reference/desktop/src/zen/spaces/
// ZenGradientGenerator.mjs. Coordinates are normalized so resizing does not
// change a saved theme's colors.
#include "krypton/ui/presentation/theme_editor.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "krypton/ui/presentation/theme_gradient.hpp"

namespace kr {
namespace {
constexpr float kTau = 2 * std::numbers::pi_v<float>;
}  // namespace

Color Hsl(float hue, float saturation, float lightness) noexcept {
  hue -= std::floor(hue);
  saturation = std::clamp(saturation, 0.0f, 1.0f);
  lightness = std::clamp(lightness, 0.0f, 1.0f);
  const auto channel = [&](float offset) {
    const float k = std::fmod(offset + hue * 12, 12.0f);
    const float a = saturation * std::min(lightness, 1 - lightness);
    return static_cast<std::uint8_t>(std::lround(
        255 *
        (lightness - a * std::max(-1.0f, std::min({k - 3, 9 - k, 1.0f})))));
  };
  return Color::FromArgb(255, channel(0), channel(8), channel(4));
}

namespace {
void Harmonize(WorkspaceTheme& theme) noexcept {
  if (!theme.point_count)
    return;
  const auto primary = ClampThemePoint(theme.points[0].position);
  const float dx = primary.x - 0.5f, dy = primary.y - 0.5f;
  const float radius = std::min(0.5f, std::hypot(dx, dy));
  const float angle = std::atan2(dy, dx);
  std::array<float, 2> offsets{};
  switch (theme.harmony) {
    case ThemeHarmony::kComplementary:
      offsets = {180, 0};
      break;
    case ThemeHarmony::kSingleAnalogous:
      offsets = {310, 0};
      break;
    case ThemeHarmony::kSplitComplementary:
      offsets = {150, 210};
      break;
    case ThemeHarmony::kAnalogous:
      offsets = {50, 310};
      break;
    case ThemeHarmony::kTriadic:
      offsets = {120, 240};
      break;
    case ThemeHarmony::kFloating:
      break;
  }
  theme.points[0].position = primary;
  for (std::uint32_t i = 0; i < theme.point_count; ++i) {
    if (i && theme.harmony != ThemeHarmony::kFloating) {
      const float at = angle + offsets[i - 1] * kTau / 360;
      theme.points[i].position = {0.5f + radius * std::cos(at),
                                  0.5f + radius * std::sin(at)};
    }
    theme.points[i].color =
        ThemePointColor(theme.points[i].position, theme.mode, theme.lightness);
  }
}

}  // namespace

bool ValidWorkspaceTheme(const WorkspaceTheme& t) noexcept {
  if (std::to_underlying(t.scheme) > std::to_underlying(ThemeScheme::kDark) ||
      std::to_underlying(t.harmony) >
          std::to_underlying(ThemeHarmony::kTriadic) ||
      std::to_underlying(t.mode) >
          std::to_underlying(ThemeColorMode::kGrayscale) ||
      t.point_count > kMaxThemePoints || !std::isfinite(t.opacity) ||
      t.opacity < design::kTokens.theme.min_opacity ||
      t.opacity > design::kTokens.theme.max_opacity ||
      !std::isfinite(t.texture) || t.texture < 0 || t.texture > 1 ||
      !std::isfinite(t.lightness) || t.lightness < 0 || t.lightness > 100)
    return false;
  for (std::size_t i = 0; i < t.point_count; ++i) {
    const auto p = t.points[i].position;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0 || p.x > 1 ||
        p.y < 0 || p.y > 1)
      return false;
  }
  return true;
}

Point ClampThemePoint(Point p) noexcept {
  if (!std::isfinite(p.x) || !std::isfinite(p.y))
    return {0.5f, 0.5f};
  const float distance = std::hypot(p.x - 0.5f, p.y - 0.5f);
  if (distance > 0.5f) {
    p.x = 0.5f + (p.x - 0.5f) * 0.5f / distance;
    p.y = 0.5f + (p.y - 0.5f) * 0.5f / distance;
  }
  return p;
}

std::optional<std::size_t> HitThemePoint(const WorkspaceTheme& theme,
                                         Point position,
                                         float side) noexcept {
  if (side <= 0 || !std::isfinite(position.x) || !std::isfinite(position.y))
    return std::nullopt;
  const auto& t = design::kTokens.theme_editor;
  std::optional<std::size_t> hit;
  float nearest = side;
  for (std::size_t i = 0; i < theme.point_count; ++i) {
    const float size = i ? t.secondary_size + 2 * t.secondary_border
                         : t.primary_size + 2 * t.primary_border;
    const float distance = std::hypot(theme.points[i].position.x - position.x,
                                      theme.points[i].position.y - position.y) *
                           side;
    if (distance <= size / 2 && distance <= nearest) {
      nearest = distance;
      hit = i;
    }
  }
  return hit;
}

Color ThemePointColor(Point p, ThemeColorMode mode, float lightness) noexcept {
  const auto& token = design::kTokens.theme_editor;
  const float side = token.color_reference_size;
  const float center = 0.5f + token.color_padding / side;
  const float radius = 0.5f + token.color_padding / (2 * side);
  const float dx = p.x + token.color_offset / side - center;
  const float dy = p.y + token.color_offset / side - center;
  const float distance = std::clamp(
      (std::hypot(dx, dy) - token.color_distance_bias / side) / radius, 0.0f,
      1.0f);
  const float hue = std::atan2(dy, dx) / kTau;
  const float saturation = mode == ThemeColorMode::kGrayscale ? 0
                           : mode == ThemeColorMode::kLightness
                               ? 1 - distance
                               : 0.9f + distance * 0.1f;
  const float l = mode == ThemeColorMode::kLightness
                      ? lightness / 100
                      : std::round(distance * 100) / 100;
  return Hsl(hue, saturation, l);
}

WorkspaceTheme ThemeFromAccent(Color color, ThemeScheme scheme) noexcept {
  WorkspaceTheme theme;
  theme.scheme = scheme;
  theme.opacity = design::kTokens.theme.opacity;
  if (color.a()) {
    theme.point_count = 1;
    theme.mode = ThemeColorMode::kPosition;
    const float r = color.r() / 255.0f, g = color.g() / 255.0f,
                b = color.b() / 255.0f;
    const float top = std::max({r, g, b}), low = std::min({r, g, b});
    const float delta = top - low;
    const float l = (top + low) / 2;
    const float s = delta == 0 ? 0 : delta / (1 - std::abs(2 * l - 1));
    float hue = 0;
    if (delta > 0)
      hue = top == r   ? std::fmod((g - b) / delta, 6.0f)
            : top == g ? (b - r) / delta + 2
                       : (r - g) / delta + 4;
    const float angle = hue * kTau / 6;
    // Zen's calculateInitialPosition places ordinary colors by hue/saturation.
    // Only explicit-lightness presets lock brightness during later dragging.
    const float radius = s / 2;
    theme.points[0] = {ClampThemePoint({0.5f + radius * std::cos(angle),
                                        0.5f + radius * std::sin(angle)}),
                       color};
    theme.lightness = l * 100;
  }
  return theme;
}

bool EditWorkspaceTheme(WorkspaceTheme& t, const ThemeEdit& e) noexcept {
  const auto before = t;
  switch (e.kind) {
    case ThemeEditKind::kMovePrimary:
      if (!t.point_count) {
        t.point_count = 1;
        t.mode = ThemeColorMode::kPosition;
      }
      t.points[0].position = ClampThemePoint(e.position);
      break;
    case ThemeEditKind::kSelectPoint:
      if (!e.index || e.index >= t.point_count)
        return false;
      std::swap(t.points[0], t.points[e.index]);
      break;
    case ThemeEditKind::kAddPoint:
      if (t.point_count == 0 || t.point_count >= kMaxThemePoints)
        return false;
      ++t.point_count;
      t.harmony = t.point_count == 2 ? ThemeHarmony::kComplementary
                                     : ThemeHarmony::kSplitComplementary;
      break;
    case ThemeEditKind::kRemovePoint:
      if (!t.point_count)
        return false;
      --t.point_count;
      t.harmony = t.point_count < 2 ? ThemeHarmony::kFloating
                                    : ThemeHarmony::kComplementary;
      break;
    case ThemeEditKind::kCycleHarmony:
      if (t.point_count < 2)
        return false;
      if (t.point_count == 2)
        t.harmony = t.harmony == ThemeHarmony::kComplementary
                        ? ThemeHarmony::kSingleAnalogous
                        : ThemeHarmony::kComplementary;
      else
        t.harmony = t.harmony == ThemeHarmony::kSplitComplementary
                        ? ThemeHarmony::kAnalogous
                    : t.harmony == ThemeHarmony::kAnalogous
                        ? ThemeHarmony::kTriadic
                        : ThemeHarmony::kSplitComplementary;
      break;
    case ThemeEditKind::kScheme:
      if (e.index > 2)
        return false;
      t.scheme = static_cast<ThemeScheme>(e.index);
      break;
    case ThemeEditKind::kOpacity:
      if (!std::isfinite(e.value))
        return false;
      t.opacity = std::clamp(e.value, design::kTokens.theme.min_opacity,
                             design::kTokens.theme.max_opacity);
      break;
    case ThemeEditKind::kTexture:
      if (!std::isfinite(e.value))
        return false;
      t.texture =
          std::round(std::clamp(e.value, 0.0f, 1.0f) * kThemeTextureSteps) /
          kThemeTextureSteps;
      if (t.texture == 1)
        t.texture = 0;
      break;
    case ThemeEditKind::kPreset: {
      if (e.index >= design::kTokens.editor_presets.size())
        return false;
      const auto& p = design::kTokens.editor_presets[e.index];
      t.point_count = static_cast<std::uint32_t>(p.points);
      t.harmony =
          static_cast<ThemeHarmony>(static_cast<std::uint32_t>(p.harmony));
      t.mode = static_cast<ThemeColorMode>(static_cast<std::uint32_t>(p.mode));
      t.lightness = p.lightness;
      const float side = design::kTokens.theme_editor.color_reference_size;
      t.points[0].position = {p.x / side, p.y / side};
      break;
    }
    case ThemeEditKind::kPreviousPage:
    case ThemeEditKind::kNextPage:
      return false;
  }
  t.customized = true;
  if (e.kind != ThemeEditKind::kScheme && e.kind != ThemeEditKind::kOpacity &&
      e.kind != ThemeEditKind::kTexture)
    Harmonize(t);
  return t != before;
}

Color SampleWorkspaceTheme(const WorkspaceTheme& t,
                           bool dark,
                           Point p) noexcept {
  std::array<Color, kMaxThemePoints> colors;
  for (std::size_t i = 0; i < t.point_count; ++i)
    colors[i] = t.points[i].color;
  const auto gradient =
      MakeThemeGradient(Color{design::PaletteFor(dark).gradient_base},
                        std::span{colors}.first(t.point_count), t.opacity);
  return SampleThemeGradient(gradient, {0, 0, 1, 1}, p);
}

Rect PlaceThemeEditor(Rect client,
                      Rect sidebar,
                      float desired_height,
                      bool on_right) noexcept {
  const auto& token = design::kTokens.theme_editor;
  const float edge =
      on_right ? sidebar.x - token.padding : sidebar.right() + token.padding;
  const float available = on_right ? edge - client.x : client.right() - edge;
  const float width = std::max(0.0f, std::min(token.width, available));
  const float height = std::clamp(
      desired_height, 0.0f, std::max(0.0f, client.height - 2 * token.padding));
  return {on_right ? edge - width : edge, client.y + token.padding, width,
          height};
}

ThemeEditorGeometry MeasureThemeEditor(float width) noexcept {
  const auto& t = design::kTokens.theme_editor;
  const float side = std::max(0.0f, width - 2 * (t.padding + t.border_width));
  const float controls_width =
      std::max(0.0f, side - 2 * (t.controls_card_padding_inline +
                                 t.controls_card_border_width));
  const float palette =
      std::max(0.0f, controls_width - 2 * (t.page_button_size + t.padding));
  return {side,
          std::clamp(
              static_cast<std::size_t>(palette / std::max(1.0f, t.swatch_size)),
              std::size_t{1}, kMaxThemePresetsPerPage),
          controls_width <
              2 * t.padding + (2 * t.dial_size_rem + t.control_gap_rem) *
                                  design::kTokens.type.size};
}

}  // namespace kr
