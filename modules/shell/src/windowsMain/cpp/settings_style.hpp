#pragma once

#include <winrt/Windows.UI.Xaml.Interop.h>

#include "view_context.hpp"

namespace kr::win {
// One retained palette for the settings window and its lazily created pages.
// Updating these brushes also updates existing cards without rebuilding them.
struct SettingsStyle {
  media::SolidColorBrush background, surface, border, text, secondary;
  media::SolidColorBrush toggle_fill, toggle_thumb;

  void Apply(const ThemeVm& theme, const Brushes& system) const {
    const auto& palette =
        theme.dark ? kTokens.settings_dark : kTokens.settings_light;
    const auto set = [](auto brush, auto color) {
      if (brush.Color() != color)
        brush.Color(color);
    };
    const bool contrast = system.high_contrast();
    set(background, contrast ? system.overlay.Color()
                             : NativeColor(Color{palette.background}));
    set(surface, contrast ? system.overlay.Color()
                          : NativeColor(Color{palette.surface}));
    set(border,
        contrast ? system.text.Color() : NativeColor(Color{palette.border}));
    set(text, contrast ? system.text.Color()
                       : NativeColor(Color{palette.foreground}));
    set(secondary, contrast ? system.muted.Color()
                            : NativeColor(Color{palette.secondary}));
    set(toggle_fill, contrast
                         ? system.active.Color()
                         : NativeColor(Color{kTokens.settings.toggle_cyan}));
    set(toggle_thumb,
        contrast ? system.text.Color()
                 : NativeColor(Color{kTokens.settings.accent_dark_text}));
  }
  void Dropdown(const controls::ComboBox& control) const {
    control.Width(kTokens.settings.dropdown_width);
    control.MaxDropDownHeight(kTokens.settings.dropdown_max_height);
    control.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    xaml::Style items{winrt::xaml_typename<controls::ComboBoxItem>()};
    items.Setters().Append(xaml::Setter(
        xaml::FrameworkElement::MaxWidthProperty(),
        winrt::box_value(
            static_cast<double>(kTokens.settings.dropdown_width))));
    control.ItemContainerStyle(items);
  }
  [[nodiscard]] controls::Viewbox Toggle(
      const controls::ToggleSwitch& control) const {
    control.OnContent(nullptr);
    control.OffContent(nullptr);
    // The native template otherwise reserves 154 DIPs for its state labels.
    // Keep its thumb, capture, keyboarding and compositor animations intact.
    control.MinWidth(0);
    control.Width(kTokens.settings.toggle_template_width);
    control.Height(kTokens.settings.toggle_template_height);
    control.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    control.VerticalAlignment(xaml::VerticalAlignment::Center);
    for (const auto key :
         {L"ToggleSwitchFillOn", L"ToggleSwitchFillOnPointerOver",
          L"ToggleSwitchFillOnPressed", L"ToggleSwitchStrokeOn",
          L"ToggleSwitchStrokeOnPointerOver", L"ToggleSwitchStrokeOnPressed"})
      control.Resources().Insert(winrt::box_value(key), toggle_fill);
    for (const auto key :
         {L"ToggleSwitchKnobFillOn", L"ToggleSwitchKnobFillOnPointerOver",
          L"ToggleSwitchKnobFillOnPressed"})
      control.Resources().Insert(winrt::box_value(key), toggle_thumb);
    controls::Viewbox box;
    box.Width(kTokens.settings.toggle_width);
    box.Height(kTokens.settings.toggle_height);
    box.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    box.VerticalAlignment(xaml::VerticalAlignment::Center);
    box.Child(control);
    return box;
  }

  [[nodiscard]] controls::Border Card(const xaml::UIElement& content) const {
    controls::Border card;
    card.Background(surface);
    card.BorderBrush(border);
    card.BorderThickness(UniformThickness(kTokens.settings.border_width));
    card.CornerRadius(UniformCorners(kTokens.settings.card_radius));
    card.Child(content);
    return card;
  }
};
}  // namespace kr::win
