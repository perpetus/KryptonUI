#pragma once
#include <optional>
#include <string_view>

#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/model/boost.hpp"

namespace kr {
void RandomizeBoost(BoostDraft& value, std::uint32_t entropy) noexcept;
[[nodiscard]] Rect PlaceBoostEditor(
    Rect client,
    std::optional<Point> origin = std::nullopt) noexcept;
enum class BoostColorPoint { kBackground, kForeground };
struct BoostPaletteState {
  float hue = 0, saturation = 0, secondary_delta = 0;
};
struct BoostPalettePoints {
  Point background, foreground;
};
[[nodiscard]] BoostPaletteState MakeBoostPalette(
    const BoostDraft& value) noexcept;
[[nodiscard]] BoostPalettePoints BoostPoints(
    const BoostPaletteState& state) noexcept;
[[nodiscard]] Color BoostSpectrumColor(Point position) noexcept;
[[nodiscard]] std::optional<BoostColorPoint> HitBoostPoint(
    const BoostPaletteState& state,
    Point position) noexcept;
[[nodiscard]] bool MoveBoostPoint(BoostPaletteState& state,
                                  BoostDraft& value,
                                  BoostColorPoint which,
                                  Point position) noexcept;
}  // namespace kr

namespace kr::boost_text {
inline constexpr std::string_view kRandomizeBoost = "Shuffle boost";
inline constexpr std::string_view kMyBoost = "My Boost";
inline constexpr std::string_view kCloseBoostEditor = "Close boost editor";
inline constexpr std::string_view kAccentColor = "Accent color";
inline constexpr std::string_view kWorkspaceColors = "Use workspace colors";
inline constexpr std::string_view kContrast = "Contrast";
inline constexpr std::string_view kBrightness = "Brightness";
inline constexpr std::string_view kOriginalSaturation = "Original saturation";
inline constexpr std::string_view kPageBackgroundColor =
    "Page background color";
inline constexpr std::string_view kTextColor = "Text color";
inline constexpr std::string_view kPageAppearance = "Invert page colors";
inline constexpr std::string_view kTextAndAccentColors = "Color settings";
inline constexpr std::string_view kEnableOrDisableBoost =
    "Enable or disable color changes";
inline constexpr std::string_view kSiteFont = "Site font";
inline constexpr std::string_view kSize = "Size";
inline constexpr std::string_view kCase = "Case";
inline constexpr std::string_view kZap = "Zap";
inline constexpr std::string_view kFocus = "Focus";
inline constexpr std::string_view kCode = "Code";
inline constexpr std::string_view kShare = "Share";
inline constexpr std::string_view kUseSiteColor = "Use site color";
inline constexpr std::string_view kName = "Name";
inline constexpr std::string_view kSiteOrUrlPattern = "Site or URL pattern";
inline constexpr std::string_view kMatch = "Match";
inline constexpr std::string_view kDomain = "Domain";
inline constexpr std::string_view kUrlContains = "URL contains";
inline constexpr std::string_view kUrlEquals = "URL equals";
inline constexpr std::string_view kRegularExpression = "Regular expression";
inline constexpr std::string_view kSave = "Save";
inline constexpr std::string_view kDeleteBoost = "Delete boost";
inline constexpr std::string_view kCustomCss = "Custom CSS";
inline constexpr std::string_view kTextSizePercent = "Text size percent";
inline constexpr std::string_view kOriginal = "Original";
inline constexpr std::string_view kUppercase = "UPPERCASE";
inline constexpr std::string_view kLowercase = "lowercase";
inline constexpr std::string_view kTitleCase = "Title Case";
inline constexpr std::string_view kUseSiteAppearance = "Use site appearance";
inline constexpr std::string_view kLight = "Light";
inline constexpr std::string_view kDark = "Dark";
inline constexpr std::string_view kAa = "Aa";
inline constexpr std::string_view kEnterANameAndAValidSiteOrUrlPattern =
    "Enter a name and a valid site or URL pattern.";
}  // namespace kr::boost_text
