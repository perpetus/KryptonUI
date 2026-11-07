#include "krypton/ui/presentation/boost.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/presentation/theme_editor.hpp"

namespace kr {
Rect PlaceBoostEditor(Rect client, std::optional<Point> origin) noexcept {
  client.width = std::max(0.0f, client.width);
  client.height = std::max(0.0f, client.height);
  if (origin && (!std::isfinite(origin->x) || !std::isfinite(origin->y)))
    origin.reset();
  const auto& t = design::kTokens.boost_editor;
  const auto columns =
      std::max(std::size_t{1}, static_cast<std::size_t>(t.font_columns));
  const auto font_rows = (kBoostFonts.size() + columns - 1) / columns;
  // Palette, color tools, font grid, typography, Zap, Focus and Code.
  const float natural_height =
      t.header_height + t.footer_height + 2 * t.border_width +
      t.body_padding_top + t.padding + t.palette_size + 5 * t.button_height +
      static_cast<float>(font_rows) * t.font_cell_height +
      2 * t.font_panel_padding + 6 * t.gap;
  const float width = std::min(t.width, std::max(0.0f, client.width));
  const float height = std::min(natural_height, std::max(0.0f, client.height));
  const Point at = origin.value_or(
      Point{client.right() - width, client.y + (client.height - height) / 2});
  return {std::clamp(at.x, client.x, client.right() - width),
          std::clamp(at.y, client.y, client.bottom() - height), width, height};
}
namespace {
constexpr float kTau = 2 * std::numbers::pi_v<float>;
constexpr auto& kPicker = design::kTokens.boost_editor;
float WrapHue(float value) noexcept {
  return value - std::floor(value);
}
float PickerRadius() noexcept {
  return kPicker.orbit_size /
         (2 * (kPicker.palette_size - 2 * kPicker.palette_padding));
}
std::pair<float, float> HueSaturation(Color color, float fallback) noexcept {
  const auto theme = ThemeFromAccent(color, ThemeScheme::kSystem);
  const auto p = theme.points[0].position;
  const float distance = std::hypot(p.x - .5f, p.y - .5f);
  return {distance > 0 ? WrapHue(std::atan2(p.y - .5f, p.x - .5f) / kTau)
                       : fallback,
          std::clamp(distance * 2, 0.0f, 1.0f)};
}
}  // namespace
BoostPaletteState MakeBoostPalette(const BoostDraft& value) noexcept {
  if (value.palette_set)
    return {value.palette_hue, value.palette_distance,
            value.secondary_rotation};
  const auto [hue, saturation] =
      value.background.a()
          ? HueSaturation(value.background, kPicker.initial_hue)
          : std::pair{kPicker.initial_hue, kPicker.initial_saturation};
  const float secondary =
      value.foreground.a()
          ? HueSaturation(value.foreground, hue + kPicker.secondary_hue_delta)
                .first
          : hue + kPicker.secondary_hue_delta;
  return {hue, saturation, WrapHue(secondary - hue)};
}
BoostPalettePoints BoostPoints(const BoostPaletteState& state) noexcept {
  const float radius = state.saturation * PickerRadius();
  const auto point = [&](float hue) {
    const float angle = (hue - kPicker.color_hue_offset / 360) * kTau;
    return Point{.5f + radius * std::cos(angle),
                 .5f + radius * std::sin(angle)};
  };
  return {point(state.hue), point(state.hue + state.secondary_delta)};
}
Color BoostSpectrumColor(Point position) noexcept {
  return Hsl(std::atan2(position.y - .5f, position.x - .5f) / kTau +
                 kPicker.color_hue_offset / 360,
             1.0f, kPicker.primary_lightness);
}
std::optional<BoostColorPoint> HitBoostPoint(const BoostPaletteState& state,
                                             Point position) noexcept {
  const auto points = BoostPoints(state);
  const float side = kPicker.palette_size - 2 * kPicker.palette_padding;
  const auto hit = [&](Point point, float size) {
    return std::hypot(point.x - position.x, point.y - position.y) * side <=
           size / 2 + kPicker.color_hit_slop;
  };
  if (hit(points.foreground, kPicker.foreground_knob))
    return BoostColorPoint::kForeground;
  if (hit(points.background, kPicker.background_knob))
    return BoostColorPoint::kBackground;
  return std::nullopt;
}
bool MoveBoostPoint(BoostPaletteState& state,
                    BoostDraft& value,
                    BoostColorPoint which,
                    Point position) noexcept {
  if (!std::isfinite(position.x) || !std::isfinite(position.y))
    return false;
  const float dx = position.x - .5f, dy = position.y - .5f;
  const float distance = std::hypot(dx, dy);
  const float hue =
      distance > 0
          ? WrapHue(std::atan2(dy, dx) / kTau + kPicker.color_hue_offset / 360)
          : state.hue;
  if (which == BoostColorPoint::kBackground) {
    state.hue = hue;
    state.saturation = std::clamp(distance / PickerRadius(), 0.0f, 1.0f);
  } else if (distance > 0) {
    state.secondary_delta = WrapHue(hue - state.hue);
  }
  const auto background =
      Hsl(state.hue, state.saturation, kPicker.primary_lightness);
  const auto foreground = Hsl(state.hue + state.secondary_delta,
                              state.saturation, kPicker.secondary_lightness);
  const bool changed = value.background != background ||
                       value.foreground != foreground || !value.enabled ||
                       !value.color_enabled || value.auto_theme ||
                       !value.palette_set || value.palette_hue != state.hue ||
                       value.palette_distance != state.saturation ||
                       value.secondary_rotation != state.secondary_delta;
  value.background = background;
  value.foreground = foreground;
  value.enabled = true;
  value.appearance = 0;
  value.color_enabled = true;
  value.auto_theme = false;
  value.palette_set = true;
  value.palette_hue = state.hue;
  value.palette_distance = state.saturation;
  value.secondary_rotation = state.secondary_delta;
  return changed;
}
void RandomizeBoost(BoostDraft& value, std::uint32_t entropy) noexcept {
  const auto& presets = design::kTokens.editor_presets;
  auto at = static_cast<std::size_t>(entropy) % presets.size();
  if (presets[at].color == value.background.argb)
    at = (at + 1) % presets.size();
  value.background = Color{presets[at].color};
  // Select a readable foreground from the existing design palettes.
  const auto brightness = 2126u * value.background.r() +
                          7152u * value.background.g() +
                          722u * value.background.b();
  const bool dark = brightness < 128u * 10000u;
  const auto& palette = dark ? design::kTokens.boost_editor.dark
                             : design::kTokens.boost_editor.light;
  value.foreground = Color{palette.text};
  value.accent = Color{palette.accent};
  value.font = (entropy >> 8) % kBoostFonts.size();
  value.appearance = 0;
  value.enabled = true;
  value.color_enabled = true;
  value.auto_theme = false;
  value.palette_set = false;
  value.brightness = static_cast<float>(entropy & 255u) / 255;
  value.saturation = static_cast<float>((entropy >> 8) & 255u) / 255;
  value.contrast = design::kTokens.boost_editor.contrast_min +
                   static_cast<float>((entropy >> 16) & 255u) / 255 *
                       (design::kTokens.boost_editor.contrast_max -
                        design::kTokens.boost_editor.contrast_min);
}
}  // namespace kr
