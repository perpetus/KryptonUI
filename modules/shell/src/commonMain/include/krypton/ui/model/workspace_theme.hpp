#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "krypton/ui/core/color.hpp"
#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/design/tokens.hpp"

namespace kr {

enum class ThemeScheme : std::uint32_t { kSystem, kLight, kDark };
enum class ThemeHarmony : std::uint32_t {
  kFloating,
  kComplementary,
  kSingleAnalogous,
  kSplitComplementary,
  kAnalogous,
  kTriadic
};
enum class ThemeColorMode : std::uint32_t { kPosition, kLightness, kGrayscale };
inline constexpr std::size_t kMaxThemePoints = 3;
inline constexpr std::size_t kThemeTextureSteps =
    static_cast<std::size_t>(design::kTokens.theme_editor.dial_dots);
static_assert(kThemeTextureSteps > 0);

struct ThemePoint {
  Point position{0.5f, 0.5f};
  Color color;
  friend bool operator==(const ThemePoint&, const ThemePoint&) = default;
};

struct WorkspaceTheme {
  bool customized = false;
  ThemeScheme scheme = ThemeScheme::kSystem;
  ThemeHarmony harmony = ThemeHarmony::kFloating;
  ThemeColorMode mode = ThemeColorMode::kPosition;
  std::uint32_t point_count = 0;
  float opacity = design::kTokens.theme.opacity;
  float texture = 0;
  float lightness = 50;
  std::array<ThemePoint, kMaxThemePoints> points{};
  friend bool operator==(const WorkspaceTheme&,
                         const WorkspaceTheme&) = default;
};

enum class ThemeEditKind {
  kMovePrimary,
  kAddPoint,
  kRemovePoint,
  kCycleHarmony,
  kScheme,
  kOpacity,
  kTexture,
  kPreset,
  kPreviousPage,
  kNextPage,
  kSelectPoint
};
struct ThemeEdit {
  ThemeEditKind kind;
  Point position;
  float value = 0;
  std::size_t index = 0;
};

}  // namespace kr
