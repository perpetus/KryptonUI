#pragma once

#include <windows.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "native_frame.hpp"
#include "onboarding_mark.hpp"
#include "onboarding_moments.hpp"
#include "onboarding_style.hpp"
#include "theme_backdrop.hpp"
#include "theme_pattern.hpp"
#include "transient_surface.hpp"

namespace kr::win {
// The full-window theme background, as the chrome islands paint it: the
// two-colour gradient, with the workspace layers on top for custom themes.
class OnboardingBackdrop {
 public:
  OnboardingBackdrop();
  void Render(const ThemeVm& theme,
              Rect client,
              Rect region,
              const OnboardingStyle& style);
  [[nodiscard]] const controls::Grid& Root() const noexcept { return root_; }

 private:
  controls::Grid root_;
  media::LinearGradientBrush gradient_;
  std::array<media::GradientStop, 2> stops_;
  media::SolidColorBrush solid_;
  ThemeBackdrop layers_;
};

// First-run onboarding over this window's real chrome. One client-sized
// island, created on demand and released after the last frame. Layers, from
// the bottom: the page (theme, Zen's surface, grain, colour field), the veil
// that hides the chrome, the ghost of a layout, the card, and landing flights.
class OnboardingView {
 public:
  OnboardingView(HWND parent, ViewContext& context, NativeFrame& frame);
  ~OnboardingView();
  OnboardingView(const OnboardingView&) = delete;
  OnboardingView& operator=(const OnboardingView&) = delete;

  void Render(const ShellViewModel& vm);
  void Raise() noexcept { island_.Raise(); }
  // The card, in client DIPs; never a window-drag area.
  [[nodiscard]] std::optional<Rect> CardBounds() const noexcept;

 private:
  void Build();
  void BuildCard();
  void BuildIgnition();
  void BuildField();
  void BuildGhost();
  void BuildFlight();
  void SetText(UiTextView text);
  [[nodiscard]] bool Animate() const noexcept;
  [[nodiscard]] Point CenterOf(const xaml::UIElement& element) const;
  void RenderBackdrops(const ShellViewModel& vm);
  void RenderHeader(const OnboardingVm& vm);
  void RenderActions(const OnboardingVm& vm);
  void RenderField(const OnboardingVm& vm);
  void RenderGhost(const ShellViewModel& vm);
  void ShowMoment(const OnboardingVm& vm, bool first);
  void PlayIgnition();
  void LayoutWords();
  void FadeSurface(bool on, float duration_ms, float delay_ms = 0);
  void Place(bool animate);
  void SpringSurface(bool animate);
  void Veil(bool on, float duration_ms, float delay_ms = 0);
  void Reveal(const ShellViewModel& vm);
  void Fly(const ShellViewModel& vm);
  void Finish(const OnboardingVm& vm);
  void Announce(const OnboardingVm& vm);
  void FocusMoment(OnboardingStep step);
  void Primary();

  ViewContext& context_;
  NativeFrame& frame_;
  TransientSurface island_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  OnboardingStyle style_;
  std::unique_ptr<OnboardingMoments> moments_;
  Choreography choreography_, content_motion_;
  MotionBatch morph_batch_, finish_batch_, reveal_batch_, content_batch_,
      bloom_batch_, warmup_batch_;

  controls::Grid root_{nullptr};
  // The page area: theme, Zen's surface, grain and the colour field.
  controls::Border page_{nullptr};
  controls::Grid page_layers_{nullptr};
  OnboardingBackdrop page_backdrop_, reveal_;
  controls::Border tint_{nullptr};
  controls::Image grain_{nullptr};
  ThemePattern grain_pattern_;
  winrt::Microsoft::UI::Composition::CompositionEllipseGeometry reveal_shape_{
      nullptr};
  std::uint32_t reveal_serial_ = 0;
  bool revealing_ = false;
  // The colour field.
  controls::Grid field_{nullptr};
  controls::ContentControl field_host_{nullptr};
  controls::Image dots_{nullptr};
  controls::Canvas handles_{nullptr};
  std::array<xaml::Shapes::Ellipse, kMaxThemePoints> points_{nullptr, nullptr,
                                                             nullptr};
  std::array<media::SolidColorBrush, kMaxThemePoints> point_colors_;
  ThemePattern dot_pattern_;
  bool dragging_ = false;
  bool points_placed_ = false;
  Rect color_field_;
  Point color_point_;
  winrt::Microsoft::UI::Composition::SpringVector3NaturalMotionAnimation
      point_spring_{nullptr},
      place_spring_{nullptr}, rail_spring_{nullptr};
  winrt::Microsoft::UI::Composition::SpringVector2NaturalMotionAnimation
      ghost_spring_{nullptr},
      surface_spring_{nullptr};
  // The veil over the real chrome: neutral first, then the theme blooms.
  controls::Grid veil_{nullptr};
  controls::Border veil_base_{nullptr};
  OnboardingBackdrop veil_theme_;
  winrt::Microsoft::UI::Composition::CompositionEllipseGeometry bloom_{nullptr};
  bool veiled_ = true;
  // The ghost of a layout.
  controls::Grid ghost_host_{nullptr};
  winrt::Microsoft::UI::Composition::ShapeVisual ghost_{nullptr};
  std::array<
      winrt::Microsoft::UI::Composition::CompositionRoundedRectangleGeometry,
      4>
      ghost_shapes_{nullptr, nullptr, nullptr, nullptr};
  winrt::Microsoft::UI::Composition::CompositionColorBrush ghost_fill_{nullptr},
      ghost_stroke_{nullptr};
  bool ghost_shown_ = false;
  std::uint32_t morph_serial_ = 0;
  // The conversation card.
  controls::Grid card_{nullptr};
  controls::Grid surface_host_{nullptr};
  winrt::Microsoft::UI::Composition::LayerVisual surface_layer_{nullptr};
  winrt::Microsoft::UI::Composition::ShapeVisual surface_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionRoundedRectangleGeometry
      surface_shape_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionColorBrush surface_fill_{
      nullptr},
      surface_stroke_{nullptr};
  controls::Grid header_{nullptr};
  std::unique_ptr<OnboardingButton> back_;
  controls::Grid rail_{nullptr};
  controls::StackPanel rail_dots_panel_{nullptr};
  controls::Border rail_indicator_{nullptr};
  bool rail_placed_ = false;
  std::vector<controls::Border> rail_dots_;
  controls::StackPanel capsule_{nullptr};
  controls::ProgressRing capsule_ring_{nullptr};
  controls::TextBlock capsule_text_{nullptr};
  controls::ScrollViewer scroll_{nullptr};
  controls::Grid content_{nullptr};
  controls::StackPanel actions_{nullptr};
  std::unique_ptr<OnboardingButton> skip_, next_;
  // Ignition: the mark, the title's words, the body and three actions.
  controls::StackPanel ignition_{nullptr};
  OnboardingMark mark_;
  controls::Canvas words_{nullptr};
  std::vector<controls::TextBlock> word_blocks_;
  controls::TextBlock ignition_body_{nullptr};
  controls::StackPanel ignition_actions_{nullptr};
  std::unique_ptr<OnboardingButton> begin_, recommended_, ignition_skip_;
  winrt::hstring ignition_title_;
  float words_width_ = 0;
  std::size_t word_count_ = 0;
  // Landing flights.
  controls::Canvas flight_layer_{nullptr};
  controls::Border flight_{nullptr}, ring_{nullptr};
  controls::Image flight_icon_{nullptr};
  controls::TextBlock flight_glyph_{nullptr};
  std::uint32_t flight_serial_ = 0;

  OnboardingStep step_ = OnboardingStep::kIgnition;
  std::uint32_t step_serial_ = 0, open_serial_ = 0;
  xaml::FrameworkElement shown_{nullptr};
  Rect client_, card_rect_;
  ShellLayout layout_;
  float scale_ = 1;
  bool built_ = false, placed_ = false, finishing_ = false, text_ready_ = false;
  bool right_to_left_ = false;
  bool primary_requests_default_ = false, primary_finishes_ = false,
       secondary_declines_ = false;
  UiTextView text_;
  ThemeVm theme_;
  std::optional<WorkspaceTheme> theme_value_;
  xaml::FrameworkElement::SizeChanged_revoker card_sized_;
  xaml::UIElement::KeyDown_revoker key_;
  xaml::UIElement::PointerMoved_revoker pointer_moved_;
  xaml::UIElement::PointerExited_revoker pointer_exited_;
  xaml::UIElement::PointerPressed_revoker field_pressed_;
  xaml::UIElement::PointerMoved_revoker field_moved_;
  xaml::UIElement::PointerReleased_revoker field_released_;
  xaml::UIElement::PointerCaptureLost_revoker field_lost_;
  xaml::UIElement::KeyDown_revoker field_key_;
  xaml::FrameworkElement::Loaded_revoker loaded_;
};
}  // namespace kr::win
