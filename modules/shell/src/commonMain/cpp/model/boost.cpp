#include "krypton/ui/model/boost.hpp"

#include <cmath>
#include <format>
#include <numbers>

#include "krypton/ui/presentation/boost.hpp"
#include "krypton/ui/presentation/theme_editor.hpp"

namespace kr {
bool ValidBoost(const BoostDraft& value) noexcept {
  const auto normalized = [](float v) {
    return std::isfinite(v) && v >= 0 && v <= 1;
  };
  return value.tab && !value.name.empty() && value.name.size() <= 256 &&
         !value.pattern.empty() && value.pattern.size() <= 2048 &&
         value.css.size() <= 256 * 1024 && value.zapped.size() <= 16384 &&
         value.focused.size() <= 4096 && value.font < kBoostFonts.size() &&
         value.size >= 50 && value.size <= 200 && value.letter_case <= 3 &&
         value.appearance <= 2 && value.match <= 3 &&
         normalized(value.contrast) && normalized(value.brightness) &&
         normalized(value.saturation) && normalized(value.palette_hue) &&
         normalized(value.palette_distance) &&
         normalized(value.secondary_rotation) && !value.name.contains('\0') &&
         !value.pattern.contains('\0') && !value.css.contains('\0') &&
         !value.zapped.contains('\0') && !value.focused.contains('\0') &&
         value.zapped.find_first_of("{}") == std::string::npos &&
         value.focused.find_first_of("{}") == std::string::npos;
}
std::string BoostCss(const BoostDraft& value) {
  if (!value.enabled)
    return {};
  std::string css = value.css;
  if (value.appearance)
    css += std::format("\n:root{{color-scheme:{} !important;}}",
                       value.appearance == 1 ? "light" : "dark");
  const auto palette = MakeBoostPalette(value);
  float hue = palette.hue;
  if (value.auto_theme && value.accent.a()) {
    const auto automatic = ThemeFromAccent(value.accent, ThemeScheme::kSystem);
    const auto p = automatic.points[0].position;
    hue = std::atan2(p.y - .5f, p.x - .5f) / (2 * std::numbers::pi_v<float>);
  }
  const bool colored =
      value.color_enabled &&
      (value.palette_set || value.background.a() || value.foreground.a() ||
       (value.auto_theme && value.accent.a()));
  if (colored || value.invert_colors) {
    const Color seed = Hsl(
        hue,
        (1 - value.saturation) * (value.auto_theme ? 1.0f : palette.saturation),
        .1f + .9f * value.brightness);
    css += std::format(
        "\n:root{{--krypton-boost-palette:{} {} {} {:.8f} {:.8f} {} "
        "!important;}}"
        "\nhtml{{background-color:Canvas;}}",
        static_cast<unsigned>(seed.r()), static_cast<unsigned>(seed.g()),
        static_cast<unsigned>(seed.b()), colored ? 1 - value.contrast : 0.0f,
        palette.secondary_delta * 360, value.invert_colors ? 1 : 0);
    if (value.invert_colors)
      css +=
          "\n:where(button,textarea,select,input:not([type=checkbox]):not(["
          "type=radio]):not([type=range]):not([type=color])){appearance:none "
          "!important;}";
  }
  constexpr std::string_view text_targets =
      ":where(body,body *):not(svg,svg "
      "*,.google-symbols,gf-load-icon-font,mat-icon,.google-material-icons,."
      "material-icons,.material-symbols-outlined,.fa,.fas,.far,.fab,"
      "[class*=\"icon\" i],[class*=\"material-symbols\" i])";
  if (value.font)
    css += std::format("\n{}{{font-family:\"{}\" !important;}}", text_targets,
                       kBoostFonts[value.font]);
  if (value.size != 100)
    css += std::format("\nhtml{{font-size:{}% !important;}}", value.size);
  if (value.letter_case)
    css += std::format("\n{}{{text-transform:{} !important;}}", text_targets,
                       value.letter_case == 1   ? "uppercase"
                       : value.letter_case == 2 ? "lowercase"
                                                : "capitalize");
  if (!value.zapped.empty())
    css += "\n" + value.zapped + "{display:none !important;}";
  if (!value.focused.empty()) {
    const auto selected = ":is(" + value.focused + ")";
    const auto page = "html:has(" + selected + ")";
    css +=
        "\n" + selected + "{width:100% !important;max-width:70vw !important;}" +
        page + "{overflow:auto !important;height:auto !important;}" + page +
        " body{display:flex !important;flex-direction:column "
        "!important;align-items:safe center !important;min-height:100vh "
        "!important;height:auto !important;overflow:visible "
        "!important;margin:0 !important;padding:48px 0 !important;}" +
        page + " body *:has(" + selected + "){display:contents !important;}" +
        page + " body *:not(" + selected + "," + selected + " *,:has(" +
        selected + ")){display:none !important;}";
  }
  return css;
}
}  // namespace kr
