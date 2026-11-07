#include "native_theme.hpp"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include "krypton/ui/presentation/tab_preview.hpp"
#include "menu_material.hpp"
#include "xaml_factory.hpp"

namespace kr::win {
namespace media = xaml::Media;

void Brushes::Apply(const ThemeVm& theme) {
  const auto& palette = *theme.palette;
  Color foreground{palette.text};
  Color subdued{palette.text_muted};
  Color field{palette.pill};
  Color selected{palette.tab_active};
  Color surface{palette.overlay};
  auto first = theme.gradient_start;
  auto last = theme.gradient_end;
  const bool contrast =
      theme.high_contrast ||
      winrt::Windows::UI::ViewManagement::AccessibilitySettings()
          .HighContrast();
  high_contrast_ = contrast;
  if (contrast) {
    const winrt::Windows::UI::ViewManagement::UISettings settings;
    const auto color = [&](auto kind) {
      const auto value = settings.GetColorValue(kind);
      return Color::FromArgb(value.A, value.R, value.G, value.B);
    };
    using Kind = winrt::Windows::UI::ViewManagement::UIColorType;
    foreground = color(Kind::Foreground);
    field = color(Kind::Background);
    subdued = foreground;
    selected = color(Kind::Accent);
    first = last = surface = field;
  }
  text.Color(NativeColor(foreground));
  muted.Color(NativeColor(subdued));
  icon.Color(NativeColor(contrast ? foreground : Color{palette.icon}));
  workspace_icon.Color(
      NativeColor(contrast ? foreground : Color{palette.workspace_icon}));
  pill.Color(NativeColor(field));
  active.Color(NativeColor(selected));
  favorite.Color(NativeColor(contrast ? field : Color{palette.favorite}));
  favorite_hover.Color(
      NativeColor(contrast ? selected : Color{palette.favorite_hover}));
  favorite_active_border.Color(NativeColor(
      contrast ? foreground : Color{palette.favorite_active_border}));
  const auto& menu = theme.dark ? kTokens.menu_dark : kTokens.menu_light;
  menu_material_kind_ = ResolveMenuMaterial(theme.menu_material);
  menu_backdrop_enabled_ =
      menu_material_kind_ != MenuMaterialKind::kSolid && !contrast;
  menu_background.Color(NativeColor(contrast ? field
                                    : menu_backdrop_enabled_
                                        ? Color{}
                                        : Color{menu.background}));
  menu_border.Color(NativeColor(contrast ? foreground : Color{menu.border}));
  overlay.Color(NativeColor(surface));
  const auto& editor = theme.dark ? kTokens.editor_dark : kTokens.editor_light;
  const auto editor_color = [&](std::uint32_t value, Color fallback) {
    return NativeColor(contrast ? fallback : Color{value});
  };
  editor_board.Color(editor_color(editor.board, field));
  editor_dots.Color(editor_color(editor.dots, foreground));
  editor_foreground.Color(editor_color(editor.foreground, foreground));
  editor_hover.Color(editor_color(editor.hover, selected));
  editor_selection.Color(editor_color(editor.selection, selected));
  editor_dial_dots.Color(editor_color(editor.dial_dots, foreground));
  editor_dial_handler.Color(editor_color(editor.dial_handler, foreground));
  editor_wave.Color(editor_color(editor.wave, foreground));
  editor_wave_muted.Color(editor_color(editor.wave_muted, foreground));
  editor_thumb.Color(editor_color(editor.thumb, foreground));
  editor_point_border.Color(editor_color(editor.point_border, foreground));
  editor_opacity_track.Color(editor_color(editor.opacity_track, field));
  const auto preview = ResolveTabPreviewColors(theme);
  preview_background.Color(NativeColor(contrast ? field : preview.background));
  preview_border.Color(NativeColor(contrast ? foreground : preview.border));
  preview_thumbnail.Color(NativeColor(contrast ? field : preview.thumbnail));
  preview_footer.Color(NativeColor(contrast ? field : Color{palette.pill}));
  const auto& search = theme.dark ? kTokens.search_dark : kTokens.search_light;
  search_background.Color(
      NativeColor(contrast ? field : Color{search.background}));
  search_border.Color(
      NativeColor(contrast ? foreground : Color{search.border}));
  search_input.Color(NativeColor(contrast ? foreground : Color{search.input}));
  search_text.Color(NativeColor(contrast ? foreground : Color{search.text}));
  search_description.Color(
      NativeColor(contrast ? foreground : Color{search.description}));
  search_url.Color(NativeColor(contrast ? foreground : Color{search.url}));
  search_hover.Color(NativeColor(contrast ? selected : Color{search.hover}));
  search_selected.Color(NativeColor(
      contrast ? selected
               : Mix(Color{theme.accent.argb & 0x00FFFFFFu}, theme.accent,
                     kTokens.overlay.selection_opacity)));
  search_selected_text.Color(
      NativeColor(contrast ? foreground : Color{search.selected_text}));
  hover.Color(NativeColor(contrast ? selected : Color{palette.button_hover}));
  pressed.Color(
      NativeColor(contrast ? selected : Color{palette.button_pressed}));
  separator.Color(
      NativeColor(contrast ? foreground : Color{palette.tab_separator}));
  close_hover.Color(NativeColor(Color{kTokens.caption.close_hover}));
  close_pressed.Color(NativeColor(Color{kTokens.caption.close_pressed}));
  close_glyph.Color(NativeColor(Color{kTokens.caption.close_glyph_hover}));
  for (const auto& gradient : {sidebar_gradient, toolbar_gradient}) {
    if (gradient.GradientStops().Size() == 0) {
      media::GradientStop start;
      start.Offset(0);
      media::GradientStop end;
      end.Offset(1);
      gradient.GradientStops().Append(start);
      gradient.GradientStops().Append(end);
    }
    gradient.GradientStops().GetAt(0).Color(NativeColor(first));
    gradient.GradientStops().GetAt(1).Color(NativeColor(last));
  }
}

void Brushes::Install(const xaml::FrameworkElement& root) const {
  const auto resources = root.Resources();
  const auto set = [&](std::wstring_view key, const auto& value) {
    resources.Insert(winrt::box_value(winrt::hstring(key)), value);
  };
  set(L"ButtonBackground", transparent);
  set(L"ButtonBackgroundPointerOver", hover);
  set(L"ButtonBackgroundPressed", pressed);
  set(L"ButtonForeground", text);
  set(L"ButtonForegroundPointerOver", text);
  set(L"ButtonForegroundPressed", text);
  set(L"ButtonBorderBrush", transparent);
  set(L"ButtonBorderBrushPointerOver", transparent);
  set(L"ButtonBorderBrushPressed", transparent);
  set(L"ListViewItemBackgroundPointerOver", transparent);
  set(L"ListViewItemBackgroundPressed", transparent);
}

media::SolidColorBrush Brushes::For(Color color) {
  const auto [entry, inserted] = colors_.try_emplace(color.argb, nullptr);
  if (inserted)
    entry->second = media::SolidColorBrush(NativeColor(color));
  return entry->second;
}

}  // namespace kr::win
