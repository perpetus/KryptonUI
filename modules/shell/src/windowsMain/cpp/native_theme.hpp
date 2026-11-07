#pragma once

#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include <unordered_map>

#include "krypton/ui/presentation/view_model.hpp"

namespace kr::win {

// Shared brush instances keep existing native controls current on theme
// changes.
class Brushes {
 public:
  void Apply(const ThemeVm& theme);
  [[nodiscard]] bool high_contrast() const noexcept { return high_contrast_; }
  [[nodiscard]] bool menu_backdrop_enabled() const noexcept {
    return menu_backdrop_enabled_;
  }
  [[nodiscard]] MenuMaterialKind menu_material() const noexcept {
    return menu_material_kind_;
  }
  void Install(const winrt::Microsoft::UI::Xaml::FrameworkElement& root) const;
  [[nodiscard]] winrt::Microsoft::UI::Xaml::Media::SolidColorBrush For(
      Color color);

  winrt::Microsoft::UI::Xaml::Media::SolidColorBrush text, muted, icon,
      workspace_icon, pill, active, transparent, overlay, hover, pressed,
      separator, close_hover, close_pressed, close_glyph, favorite,
      favorite_hover, favorite_active_border, menu_background, menu_border;
  winrt::Microsoft::UI::Xaml::Media::SolidColorBrush search_background,
      search_border, search_input, search_text, search_description, search_url,
      search_hover, search_selected, search_selected_text;
  winrt::Microsoft::UI::Xaml::Media::SolidColorBrush preview_background,
      preview_border, preview_thumbnail, preview_footer;
  winrt::Microsoft::UI::Xaml::Media::SolidColorBrush editor_board, editor_dots,
      editor_foreground, editor_hover, editor_selection, editor_dial_dots,
      editor_dial_handler, editor_wave, editor_wave_muted, editor_thumb,
      editor_point_border, editor_opacity_track;
  winrt::Microsoft::UI::Xaml::Media::LinearGradientBrush sidebar_gradient,
      toolbar_gradient;

 private:
  bool high_contrast_ = false;
  bool menu_backdrop_enabled_ = false;
  MenuMaterialKind menu_material_kind_ = MenuMaterialKind::kDefault;
  std::unordered_map<std::uint32_t,
                     winrt::Microsoft::UI::Xaml::Media::SolidColorBrush>
      colors_;
};

}  // namespace kr::win
