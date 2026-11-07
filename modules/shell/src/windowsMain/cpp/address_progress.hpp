#pragma once

#include <winrt/Microsoft.UI.Composition.h>

#include <array>
#include <memory>
#include <optional>

#include "view_context.hpp"

namespace kr::win {

namespace composition = winrt::Microsoft::UI::Composition;

// Retained compositor visuals; navigation rules remain in commonMain.
class AddressProgress {
 public:
  explicit AddressProgress(controls::Grid root);
  ~AddressProgress();

  void Render(const ShellViewModel::LoadingProgressVm& vm,
              bool rtl,
              bool high_contrast,
              bool refresh_appearance);

 private:
  struct Completion {
    composition::CompositionScopedBatch batch{nullptr};
    winrt::event_token token{};
  };

  [[nodiscard]] bool Ensure() noexcept;
  void Initialize();
  void UpdateClip() noexcept;
  void Bind(const composition::CompositionObject& object,
            std::wstring_view property,
            std::wstring_view expression);
  void Palette(const ShellViewModel::LoadingProgressVm& vm, bool high_contrast);
  void AnimateProgress(float target);
  void Shimmer(bool active);
  void Fade(float opacity);
  void Finish();
  void Exit();
  static void ResetBatch(Completion& completion) noexcept;
  void Stop();
  void Hide() noexcept;

  controls::Grid root_;
  composition::Compositor compositor_{nullptr};
  composition::Visual host_{nullptr};
  composition::ContainerVisual visual_{nullptr}, fill_host_{nullptr};
  composition::RectangleClip clip_{nullptr};
  composition::SpriteVisual fallback_{nullptr}, fill_{nullptr},
      shimmer_{nullptr}, glow_{nullptr}, glow_shimmer_{nullptr};
  composition::CompositionPropertySet metrics_{nullptr};
  composition::CompositionColorBrush fallback_brush_{nullptr};
  composition::CompositionLinearGradientBrush fill_brush_{nullptr},
      shimmer_brush_{nullptr};
  composition::CompositionRadialGradientBrush radial_mask_{nullptr};
  std::array<composition::CompositionColorGradientStop, 2> fill_stops_{nullptr,
                                                                       nullptr};
  std::array<composition::CompositionColorGradientStop, 3> shimmer_stops_{
      nullptr, nullptr, nullptr};
  composition::ScalarKeyFrameAnimation progress_animation_{nullptr},
      fade_animation_{nullptr}, shimmer_animation_{nullptr};
  composition::CubicBezierEasingFunction progress_easing_{nullptr},
      fade_easing_{nullptr};
  Completion progress_completion_, fade_completion_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  xaml::FrameworkElement::SizeChanged_revoker clip_sized_;
  std::optional<LoadingProgressState> last_;
  Color primary_, secondary_, foreground_;
  std::uint64_t animation_generation_ = 0;
  float target_ = 0, pending_progress_ = 0;
  bool requested_ = false, visible_ = false, finishing_ = false, ended_ = false;
  bool animations_ = true, shimmering_ = false, progress_running_ = false;
  bool rtl_ = false, contrast_ = false, palette_ready_ = false;
  bool gradients_available_ = false, initialized_ = false;
};

}  // namespace kr::win
