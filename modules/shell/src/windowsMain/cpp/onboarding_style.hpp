#pragma once

#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <cstdint>
#include <memory>

#include "krypton/ui/presentation/onboarding_icons.hpp"
#include "view_context.hpp"

namespace kr::win {
// Onboarding brushes. Controls keep these instances, so a theme change only
// updates colours. High contrast swaps to the system colours Brushes chose.
struct OnboardingStyle {
  void Apply(const ThemeVm& theme, const Brushes& chrome);
  [[nodiscard]] Color card_color() const noexcept { return card_color_; }
  [[nodiscard]] Color border_color() const noexcept { return border_color_; }
  [[nodiscard]] Color accent_color() const noexcept { return accent_color_; }
  [[nodiscard]] bool high_contrast() const noexcept { return high_contrast_; }

  media::SolidColorBrush card, field, tile, border, text, secondary, radio,
      accent, on_accent, transparent, neutral;

 private:
  Color card_color_, border_color_, accent_color_;
  bool high_contrast_ = false;
};

// Text in the onboarding's type scale; sizes are rem tokens.
[[nodiscard]] controls::TextBlock OnboardingText(const OnboardingStyle& style,
                                                 float rem,
                                                 bool strong = false,
                                                 bool secondary = false);

// Zen's .zen-big-accent-button: primary uses the accent fill, the others the
// accent text. Hover grows it slightly unless motion is off.
struct OnboardingButton {
  controls::Button control;
  controls::TextBlock label;
  controls::Button::Click_revoker click;
  xaml::UIElement::PointerEntered_revoker entered;
  xaml::UIElement::PointerExited_revoker exited;
  xaml::FrameworkElement::SizeChanged_revoker sized;
};
[[nodiscard]] std::unique_ptr<OnboardingButton> MakeOnboardingButton(
    const OnboardingStyle& style,
    ViewContext& context,
    bool primary,
    Task action);

// A selectable card: a radio button filling Zen's option card, with an
// optional icon or picture and a secondary line.
struct OnboardingChoice {
  controls::Border frame;
  controls::RadioButton radio;
  controls::Image picture{nullptr};
  controls::Border icon_tile{nullptr};
  controls::Image icon{nullptr};
  controls::Viewbox brand_view{nullptr};
  const OnboardingIcon* brand = nullptr;
  controls::TextBlock label, detail{nullptr};
  std::uint64_t id = 0;
  controls::RadioButton::Checked_revoker checked;
  xaml::UIElement::PointerEntered_revoker entered;
  xaml::UIElement::PointerExited_revoker exited;
  // Previewing the answer while it is pointed at or focused.
  xaml::UIElement::PointerEntered_revoker hovered;
  xaml::UIElement::PointerExited_revoker unhovered;
  xaml::UIElement::GotFocus_revoker focused;
  xaml::UIElement::LostFocus_revoker unfocused;
  xaml::FrameworkElement::SizeChanged_revoker sized;
};
struct OnboardingChoiceOptions {
  bool icon = false, picture = false, detail = false, compact = false;
};
[[nodiscard]] std::unique_ptr<OnboardingChoice> MakeOnboardingChoice(
    const OnboardingStyle& style,
    ViewContext& context,
    std::wstring_view group,
    std::uint64_t id,
    OnboardingChoiceOptions options,
    std::move_only_function<void(std::uint64_t)> chosen);
// Checks the card and paints Zen's checked accent, without notifying.
void SelectOnboardingChoice(OnboardingChoice& choice,
                            const OnboardingStyle& style,
                            bool selected);
}  // namespace kr::win
