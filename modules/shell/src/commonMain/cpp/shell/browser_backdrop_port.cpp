#include <array>

#include "krypton/ui/design/grain.generated.hpp"
#include "krypton/ui/presentation/multi_page.hpp"
#include "krypton/ui/presentation/theme_gradient.hpp"
#include "krypton/ui/presentation/view_model.hpp"
#include "krypton/ui/shell/browser_port.hpp"

namespace kr {
void BrowserPort::SetWindowBackdrop(const ThemeVm& theme) const {
  if (!KR_HAS_SLOT(&table_, KrBrowser, set_window_backdrop) ||
      !table_.set_window_backdrop)
    return;
  const auto* workspace = theme.workspace_theme;
  std::array<Color, 3> colors{};
  if (workspace && !theme.high_contrast) {
    for (std::size_t index = 0; index < workspace->point_count; ++index)
      colors[index] = workspace->points[index].color;
  }
  auto gradient = MakeThemeGradient(
      Color{theme.palette->gradient_base},
      std::span{colors}.first(
          workspace && !theme.high_contrast ? workspace->point_count : 0),
      workspace ? workspace->opacity : 0);
  KrWindowBackdrop result{};
  std::array<KrGradientLayer, 3> layers{};
  result.struct_size = sizeof(result);
  result.layers = layers.data();
  result.layer_size = sizeof(KrGradientLayer);
  result.base = gradient.base.argb;
  result.layer_count = gradient.layer_count;
  for (std::size_t index = 0; index < gradient.layer_count; ++index) {
    const auto& layer = gradient.layers[index];
    layers[index] = {sizeof(KrGradientLayer),
                     static_cast<std::uint32_t>(layer.kind),
                     layer.color.argb,
                     layer.angle,
                     layer.center_x,
                     layer.start,
                     layer.end};
  }
  if (!workspace) {
    result.base = theme.gradient_end.argb;
    if (theme.gradient_start != theme.gradient_end) {
      result.layer_count = 1;
      layers[0] = {sizeof(KrGradientLayer),
                   KR_GRADIENT_DIAGONAL,
                   theme.gradient_start.argb,
                   0,
                   0,
                   0,
                   1};
    }
  }
  result.texture = workspace && workspace->customized && !theme.high_contrast
                       ? workspace->texture
                       : 0;
  if (result.texture > 0) {
    result.grain = {
        sizeof(KrImage),
        0,
        design::kGrainWidth,
        design::kGrainHeight,
        design::kGrainWidth * 4,
        reinterpret_cast<const std::uint8_t*>(design::kGrainPixels.data())};
  }
  result.pane_outline = SplitOutlineColor(theme).argb;
  const auto glance = GlanceButtonColors(theme);
  result.glance_button = glance.button.argb;
  result.glance_button_hover = glance.button_hover.argb;
  result.glance_button_icon = glance.button_icon.argb;
  result.glance_shadow = glance.shadow.argb;
  result.glance_button_shadow = glance.button_shadow.argb;
  result.flags = theme.transparent_chrome ? KR_WINDOW_BACKDROP_TRANSPARENT : 0;

  table_.set_window_backdrop(table_.self, &result);
}
}  // namespace kr
