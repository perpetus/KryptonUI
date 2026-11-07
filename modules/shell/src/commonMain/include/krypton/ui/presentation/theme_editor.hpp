#pragma once

#include <optional>

#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/model/workspace_theme.hpp"

namespace kr {

inline constexpr std::size_t kThemePresetPageCount = [] {
  std::size_t result = 0;
  for (const auto& p : design::kTokens.editor_presets)
    result = std::max(result, static_cast<std::size_t>(p.page) + 1);
  return result;
}();
inline constexpr std::size_t kMaxThemePresetsPerPage = [] {
  std::array<std::size_t, kThemePresetPageCount> counts{};
  for (const auto& p : design::kTokens.editor_presets)
    ++counts[static_cast<std::size_t>(p.page)];
  return *std::max_element(counts.begin(), counts.end());
}();
struct ThemeEditorGeometry {
  float canvas_side = 0;
  std::size_t preset_columns = 1;
  bool stacked_controls = false;
};
[[nodiscard]] ThemeEditorGeometry MeasureThemeEditor(float width) noexcept;

[[nodiscard]] bool ValidWorkspaceTheme(const WorkspaceTheme& theme) noexcept;
[[nodiscard]] WorkspaceTheme ThemeFromAccent(Color color,
                                             ThemeScheme scheme) noexcept;
[[nodiscard]] Point ClampThemePoint(Point point) noexcept;
[[nodiscard]] std::optional<std::size_t>
HitThemePoint(const WorkspaceTheme& theme, Point position, float side) noexcept;
// Hue is a turn fraction; saturation and lightness are 0..1. Opaque.
[[nodiscard]] Color Hsl(float hue, float saturation, float lightness) noexcept;
[[nodiscard]] Color ThemePointColor(Point point,
                                    ThemeColorMode mode,
                                    float lightness) noexcept;
[[nodiscard]] bool EditWorkspaceTheme(WorkspaceTheme& theme,
                                      const ThemeEdit& edit) noexcept;
[[nodiscard]] Color SampleWorkspaceTheme(const WorkspaceTheme& theme,
                                         bool dark,
                                         Point position) noexcept;
[[nodiscard]] Rect PlaceThemeEditor(Rect client,
                                    Rect sidebar,
                                    float desired_height,
                                    bool on_right) noexcept;

}  // namespace kr
