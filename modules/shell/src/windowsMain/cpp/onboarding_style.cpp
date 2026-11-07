#include "onboarding_style.hpp"

#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.Foundation.Collections.h>

#include "krypton/ui/presentation/library.hpp"
#include "krypton/ui/presentation/onboarding.hpp"

namespace kr::win {
namespace {
const auto& Onboarding() noexcept {
  return kTokens.onboarding;
}

// Pointer-over and pressed states keep the resting colours; Zen's feedback
// is the scale, not a fill change.
void HoldColors(const controls::Control& control,
                const media::Brush& background,
                const media::Brush& foreground,
                const media::Brush& border) {
  const auto resources = control.Resources();
  for (const auto* state : {L"PointerOver", L"Pressed"}) {
    const std::wstring name(state);
    resources.Insert(winrt::box_value(L"ButtonBackground" + name), background);
    resources.Insert(winrt::box_value(L"ButtonForeground" + name), foreground);
    resources.Insert(winrt::box_value(L"ButtonBorderBrush" + name), border);
    resources.Insert(winrt::box_value(L"RadioButtonBackground" + name),
                     background);
    resources.Insert(winrt::box_value(L"RadioButtonForeground" + name),
                     foreground);
  }
}

// A scale transition that grows the element around its centre on hover.
template <typename Holder>
void GrowOnHover(Holder& holder,
                 const xaml::UIElement& element,
                 const xaml::FrameworkElement& sized,
                 ViewContext& context,
                 const OnboardingStyle& style,
                 float scale,
                 float duration_ms) {
  xaml::Vector3Transition transition;
  transition.Duration(std::chrono::duration_cast<foundation::TimeSpan>(
      std::chrono::duration<float, std::milli>(duration_ms)));
  element.ScaleTransition(transition);
  holder.sized = sized.SizeChanged(
      winrt::auto_revoke,
      [element](auto&&, const xaml::SizeChangedEventArgs& args) {
        element.CenterPoint({static_cast<float>(args.NewSize().Width) / 2,
                             static_cast<float>(args.NewSize().Height) / 2, 0});
      });
  holder.entered = element.PointerEntered(
      winrt::auto_revoke, [element, &context, &style, scale](auto&&, auto&&) {
        if (context.motion.Enabled(style.high_contrast()))
          element.Scale({scale, scale, 1});
      });
  holder.exited = element.PointerExited(
      winrt::auto_revoke,
      [element](auto&&, auto&&) { element.Scale({1, 1, 1}); });
}
}  // namespace

void OnboardingStyle::Apply(const ThemeVm& theme, const Brushes& chrome) {
  const auto& palette = OnboardingPaletteFor(theme.dark);
  high_contrast_ = chrome.high_contrast();
  card_color_ = Color{palette.card};
  border_color_ = Color{palette.border};
  accent_color_ = AccentButtonColor(theme.accent, theme.dark);
  Color field_color{palette.field}, tile_color{palette.tile},
      text_color{palette.text}, radio_color{palette.radio};
  Color on_accent_color = Color{kTokens.onboarding.light.card};
  if (high_contrast_) {
    // Brushes resolved the system colours already.
    const auto system = [](const media::SolidColorBrush& brush) {
      const auto value = brush.Color();
      return Color::FromArgb(value.A, value.R, value.G, value.B);
    };
    card_color_ = field_color = tile_color = system(chrome.pill);
    text_color = radio_color = border_color_ = system(chrome.text);
    accent_color_ = system(chrome.active);
    on_accent_color = card_color_;
  }
  card.Color(NativeColor(card_color_));
  field.Color(NativeColor(field_color));
  tile.Color(NativeColor(tile_color));
  border.Color(NativeColor(border_color_));
  text.Color(NativeColor(text_color));
  secondary.Color(NativeColor(
      high_contrast_ ? text_color
                     : text_color.WithAlpha(static_cast<std::uint8_t>(
                           Onboarding().secondary_opacity * 255))));
  radio.Color(NativeColor(radio_color));
  accent.Color(NativeColor(accent_color_));
  on_accent.Color(NativeColor(on_accent_color));
  transparent.Color(NativeColor(Color{}));
  neutral.Color(NativeColor(
      high_contrast_ ? card_color_
                     : Color{design::PaletteFor(theme.dark).gradient_base}));
}

controls::TextBlock OnboardingText(const OnboardingStyle& style,
                                   float rem,
                                   bool strong,
                                   bool secondary) {
  auto text = Label(L"", OnboardingRem(rem));
  text.TextWrapping(xaml::TextWrapping::Wrap);
  text.Foreground(secondary ? style.secondary : style.text);
  if (strong)
    text.FontWeight(winrt::Windows::UI::Text::FontWeight{700});
  return text;
}

std::unique_ptr<OnboardingButton> MakeOnboardingButton(
    const OnboardingStyle& style,
    ViewContext& context,
    bool primary,
    Task action) {
  auto result = std::make_unique<OnboardingButton>();
  auto& button = result->control;
  const auto& t = Onboarding();
  button.CornerRadius(UniformCorners(t.button_radius));
  button.Padding({OnboardingRem(t.button_padding_inline_rem),
                  OnboardingRem(t.button_padding_block_rem) / 2,
                  OnboardingRem(t.button_padding_inline_rem),
                  OnboardingRem(t.button_padding_block_rem) / 2});
  button.BorderThickness(UniformThickness(0));
  button.Background(primary ? style.accent : style.transparent);
  button.Foreground(primary ? style.on_accent : style.accent);
  HoldColors(button, primary ? style.accent : style.transparent,
             primary ? style.on_accent : style.accent, style.transparent);
  result->label = Label(L"", OnboardingRem(t.button_rem));
  result->label.FontWeight(kSemiBold);
  result->label.TextWrapping(xaml::TextWrapping::Wrap);
  result->label.TextAlignment(xaml::TextAlignment::Center);
  button.Content(result->label);
  result->click = button.Click(
      winrt::auto_revoke, [action = std::make_shared<Task>(std::move(action))](
                              auto&&, auto&&) { (*action)(); });
  GrowOnHover(*result, button, button, context, style, t.button_scale,
              t.button_scale_ms);
  return result;
}

std::unique_ptr<OnboardingChoice> MakeOnboardingChoice(
    const OnboardingStyle& style,
    ViewContext& context,
    std::wstring_view group,
    std::uint64_t id,
    OnboardingChoiceOptions options,
    std::move_only_function<void(std::uint64_t)> chosen) {
  auto result = std::make_unique<OnboardingChoice>();
  auto& choice = *result;
  const auto& t = Onboarding();
  choice.id = id;
  const float block = options.compact
                          ? OnboardingRem(t.card_gap_rem) / 2
                          : OnboardingRem(t.option_padding_block_rem) / 2;
  const float inline_padding =
      options.compact ? OnboardingRem(t.card_gap_rem) / 2
                      : OnboardingRem(t.option_padding_inline_rem) / 2;
  choice.frame.CornerRadius(UniformCorners(t.option_radius));
  choice.frame.BorderThickness(UniformThickness(1));
  choice.frame.BorderBrush(style.border);
  choice.frame.Background(style.card);
  choice.radio.GroupName(winrt::hstring(group));
  choice.radio.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  choice.radio.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
  choice.radio.Padding({inline_padding, block, inline_padding, block});
  choice.radio.Foreground(style.text);
  choice.radio.Background(style.transparent);
  HoldColors(choice.radio, style.transparent, style.text, style.transparent);
  choice.radio.Resources().Insert(
      winrt::box_value(L"RadioButtonOuterEllipseStroke"), style.radio);
  choice.radio.Resources().Insert(
      winrt::box_value(L"RadioButtonOuterEllipseCheckedStroke"), style.accent);
  choice.radio.Resources().Insert(
      winrt::box_value(L"RadioButtonOuterEllipseCheckedFill"), style.accent);

  controls::StackPanel content;
  content.Spacing(OnboardingRem(t.card_gap_rem) / 2);
  controls::StackPanel row;
  row.Orientation(controls::Orientation::Horizontal);
  row.Spacing(OnboardingRem(t.card_gap_rem));
  if (options.picture) {
    choice.picture = controls::Image();
    choice.picture.Stretch(media::Stretch::Uniform);
    content.Children().Append(choice.picture);
  }
  if (options.icon) {
    choice.icon_tile = controls::Border();
    choice.icon_tile.Width(t.icon_size + 2 * OnboardingRem(t.card_gap_rem) / 2);
    choice.icon_tile.Height(choice.icon_tile.Width());
    choice.icon_tile.CornerRadius(UniformCorners(t.icon_radius));
    choice.icon_tile.Background(style.tile);
    choice.icon = controls::Image();
    choice.icon.Width(t.icon_size);
    choice.icon.Height(t.icon_size);
    choice.icon.Stretch(media::Stretch::Uniform);
    choice.icon_tile.Child(choice.icon);
    row.Children().Append(choice.icon_tile);
  }
  controls::StackPanel words;
  words.VerticalAlignment(xaml::VerticalAlignment::Center);
  choice.label = OnboardingText(style, options.compact ? 1 : t.option_rem);
  choice.label.FontWeight(kSemiBold);
  words.Children().Append(choice.label);
  if (options.detail) {
    choice.detail = OnboardingText(style, 1, false, true);
    words.Children().Append(choice.detail);
  }
  row.Children().Append(words);
  content.Children().Append(row);
  choice.radio.Content(content);
  choice.frame.Child(choice.radio);

  choice.checked = choice.radio.Checked(
      winrt::auto_revoke,
      [id,
       chosen = std::make_shared<std::move_only_function<void(std::uint64_t)>>(
           std::move(chosen))](auto&&, auto&&) { (*chosen)(id); });
  GrowOnHover(choice, choice.frame, choice.frame, context, style, t.card_scale,
              t.card_scale_ms);
  return result;
}

void SelectOnboardingChoice(OnboardingChoice& choice,
                            const OnboardingStyle& style,
                            bool selected) {
  const auto checked = choice.radio.IsChecked();
  if (!checked || checked.Value() != selected) {
    // Reconciling: the Checked handler compares against the view model.
    choice.radio.IsChecked(selected);
  }
  choice.frame.BorderBrush(selected ? style.accent : style.border);
  choice.label.Foreground(selected ? style.accent : style.text);
}
}  // namespace kr::win
