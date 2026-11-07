#pragma once

#include <array>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "onboarding_style.hpp"
#include "settings_privacy.hpp"

namespace kr::win {
// The question each moment asks: its title, body and controls. Panels are
// built on first visit and kept until onboarding is released; rendering only
// reconciles what changed. Every choice goes to the delegate; nothing here
// changes state on its own.
class OnboardingMoments {
 public:
  // Converts an element's centre to window client DIPs.
  using ClientCenter = std::move_only_function<Point(const xaml::UIElement&)>;
  OnboardingMoments(ViewContext& context,
                    OnboardingStyle& style,
                    ClientCenter center);
  ~OnboardingMoments();
  OnboardingMoments(const OnboardingMoments&) = delete;
  OnboardingMoments& operator=(const OnboardingMoments&) = delete;

  // The moment's panel, built on first use. Not for the Ignition bookend.
  [[nodiscard]] xaml::FrameworkElement Panel(OnboardingStep step);
  void Render(const ShellViewModel& vm, bool text_changed);
  // The parts that rise in, in order: title, body, controls.
  [[nodiscard]] std::span<const xaml::UIElement> Parts(
      OnboardingStep step) const noexcept;
  // The selected answer, where a landing flight leaves from.
  [[nodiscard]] xaml::UIElement Source(OnboardingStep step) const;
  // Where keyboard focus goes when the moment arrives.
  [[nodiscard]] controls::Control FirstFocus(OnboardingStep step) const;

 private:
  struct Moment {
    controls::StackPanel panel{nullptr};
    controls::TextBlock title{nullptr}, body{nullptr};
    std::array<xaml::UIElement, 3> parts{nullptr, nullptr, nullptr};
  };
  using Choices = std::vector<std::unique_ptr<OnboardingChoice>>;
  [[nodiscard]] static std::size_t Index(OnboardingStep step) noexcept;
  Moment& Build(OnboardingStep step);
  xaml::FrameworkElement BuildShape();
  xaml::FrameworkElement BuildColor();
  xaml::FrameworkElement BuildImport();
  xaml::FrameworkElement BuildSearch();
  xaml::FrameworkElement BuildPrivacy();
  xaml::FrameworkElement BuildDefault();
  xaml::FrameworkElement BuildReady();
  void SetText(UiTextView text);
  void RenderShape(const OnboardingVm& vm, UiTextView text);
  void RenderColor(const OnboardingVm& vm);
  void RenderImport(const ShellViewModel& vm);
  void RenderSearch(const ShellViewModel& vm);
  [[nodiscard]] media::ImageSource BundledImage(const OnboardingIcon* icon,
                                                bool dark);
  void SetChoiceIcon(OnboardingChoice& choice,
                     ImageId host_icon,
                     const ShellViewModel& vm,
                     const OnboardingIcon* icon);
  void RenderPrivacy(const ShellViewModel& vm, bool text_changed);
  void RenderDefault(const OnboardingVm& vm, UiTextView text);
  void RenderReady(const OnboardingVm& vm, UiTextView text);
  [[nodiscard]] std::unique_ptr<OnboardingChoice> Choice(
      std::wstring_view group,
      std::uint64_t id,
      OnboardingChoiceOptions options,
      std::move_only_function<void(std::uint64_t)> chosen);

  ViewContext& context_;
  OnboardingStyle& style_;
  ClientCenter center_;
  std::array<Moment, kOnboardingStepCount> moments_;
  bool reconciling_ = false;

  // Shape.
  Choices layouts_;
  controls::ToggleSwitch side_{nullptr};
  controls::ToggleSwitch::Toggled_revoker side_toggled_;
  // Color.
  std::array<std::unique_ptr<NativeButton>, 3> schemes_;
  std::vector<std::unique_ptr<NativeButton>> swatches_;
  std::vector<media::SolidColorBrush> swatch_colors_;
  std::unique_ptr<NativeButton> add_, remove_, harmony_;
  controls::StackPanel swatch_row_{nullptr};
  std::size_t first_preset_ = 0;
  // Import.
  controls::StackPanel sources_panel_{nullptr}, items_panel_{nullptr};
  Choices sources_;
  std::unique_ptr<OnboardingChoice> fresh_;
  struct Item {
    controls::CheckBox box{nullptr};
    controls::CheckBox::Checked_revoker checked;
    controls::CheckBox::Unchecked_revoker unchecked;
  };
  std::array<Item, kImportDataCount> items_;
  // Search.
  controls::StackPanel engines_panel_{nullptr};
  Choices engines_;
  struct CachedBrandImage {
    media::ImageSource light{nullptr}, dark{nullptr};
    bool tried_light = false, tried_dark = false;
  };
  std::unordered_map<const OnboardingIcon*, CachedBrandImage> brand_images_;
  // Privacy reuses the settings rows over the onboarding records.
  std::unique_ptr<PrivacySettings> privacy_;
  // Default browser.
  controls::TextBlock default_glyph_{nullptr}, default_status_{nullptr};
  // Ready.
  controls::StackPanel summary_{nullptr};
  std::vector<controls::TextBlock> summary_lines_;
  controls::Button relaunch_{nullptr};
  controls::Button::Click_revoker relaunch_click_;
  bool text_ready_ = false;
};
}  // namespace kr::win
