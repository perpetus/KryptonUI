#pragma once

#include "view_context.hpp"

namespace kr::win {
// One retained palette for the settings window and its lazily created pages.
// Updating these brushes also updates existing cards without rebuilding them.
struct SettingsStyle {
  media::SolidColorBrush background, surface, border, text, secondary;

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
