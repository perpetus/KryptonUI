#include "address_progress.hpp"

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <utility>

#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
namespace hosting = xaml::Hosting;

namespace {
auto Duration(float milliseconds) {
  return std::chrono::milliseconds(static_cast<int>(milliseconds));
}

Color Opacity(Color color, float opacity) {
  return color.WithAlpha(static_cast<std::uint8_t>(
      std::lround(color.a() * std::clamp(opacity, 0.0f, 1.0f))));
}

}  // namespace

AddressProgress::AddressProgress(controls::Grid root) : root_(std::move(root)) {
  root_.IsHitTestVisible(false);
  root_.FlowDirection(xaml::FlowDirection::LeftToRight);
  root_.Visibility(xaml::Visibility::Collapsed);
  xaml::Automation::AutomationProperties::SetAccessibilityView(
      root_, xaml::Automation::Peers::AccessibilityView::Raw);
}

AddressProgress::~AddressProgress() {
  lifetime_.reset();
  clip_sized_.revoke();
  try {
    Stop();
    if (visual_)
      hosting::ElementCompositionPreview::SetElementChildVisual(root_, nullptr);
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kError,
                              "Address progress could not close");
  }
}

void AddressProgress::Bind(const composition::CompositionObject& object,
                           std::wstring_view property,
                           std::wstring_view expression) {
  auto animation =
      compositor_.CreateExpressionAnimation(winrt::hstring(expression));
  animation.SetReferenceParameter(L"m", metrics_);
  animation.SetReferenceParameter(L"host", host_);
  object.StartAnimation(winrt::hstring(property), animation);
}

bool AddressProgress::Ensure() noexcept {
  if (initialized_)
    return true;
  try {
    Initialize();
    initialized_ = true;
    return true;
  } catch (const winrt::hresult_error& error) {
    std::array<char, 96> message{};
    std::snprintf(message.data(), message.size(),
                  "Address progress setup failed: 0x%08X",
                  static_cast<unsigned int>(error.code().value));
    platform::WriteDefaultLog(LogLevel::kError, message.data());
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kError,
                              "Address progress setup failed");
  }
  palette_ready_ = false;
  clip_sized_.revoke();
  Hide();
  return false;
}

void AddressProgress::UpdateClip() noexcept {
  if (!clip_)
    return;
  try {
    const float width = std::max(0.0f, static_cast<float>(root_.ActualWidth()));
    const float height =
        std::max(0.0f, static_cast<float>(root_.ActualHeight()));
    const winrt::Windows::Foundation::Numerics::float2 radius{
        std::min(kTokens.radius.pill, width / 2),
        std::min(kTokens.radius.pill, height / 2)};
    clip_.Right(width);
    clip_.Bottom(height);
    clip_.TopLeftRadius(radius);
    clip_.TopRightRadius(radius);
    clip_.BottomLeftRadius(radius);
    clip_.BottomRightRadius(radius);
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kError,
                              "Address progress clip could not update");
  }
}

void AddressProgress::Initialize() {
  gradients_available_ = false;
  const auto& t = kTokens.loading_progress;
  host_ = hosting::ElementCompositionPreview::GetElementVisual(root_);
  compositor_ = host_.Compositor();
  metrics_ = compositor_.CreatePropertySet();
  metrics_.InsertScalar(L"Progress", 0);
  metrics_.InsertScalar(L"Shimmer", 0);
  metrics_.InsertScalar(L"Radius", kTokens.radius.pill);
  metrics_.InsertScalar(L"Bar", t.bar_height);
  metrics_.InsertScalar(L"WaveStart", t.wave_start);
  metrics_.InsertScalar(L"WaveEnd", t.wave_end);
  metrics_.InsertScalar(L"WaveWidth", t.wave_width);
  metrics_.InsertScalar(L"FrontRadiusX", t.highlight_radius_x);
  metrics_.InsertScalar(L"FrontRadiusY", t.highlight_radius_y);

  visual_ = compositor_.CreateContainerVisual();
  visual_.RelativeSizeAdjustment({1, 1});
  visual_.Opacity(0);
  Bind(visual_, L"CenterPoint", L"Vector3(host.Size.X / 2, 0, 0)");
  clip_ = compositor_.CreateRectangleClip();
  clip_.Left(0);
  clip_.Top(0);
  UpdateClip();
  visual_.Clip(clip_);
  const std::weak_ptr<int> weak = lifetime_;
  clip_sized_ = root_.SizeChanged(winrt::auto_revoke,
                                  [this, weak](auto&&, auto&&) noexcept {
                                    if (!weak.expired())
                                      UpdateClip();
                                  });

  fill_host_ = compositor_.CreateContainerVisual();
  Bind(fill_host_, L"Size",
       L"Vector2(Max(0, host.Size.X * Clamp(m.Progress, 0, 1)), "
       L"Min(host.Size.Y, m.Bar))");
  Bind(fill_host_, L"Offset", L"Vector3(0, Max(0, host.Size.Y - m.Bar), 0)");
  fill_host_.Clip(compositor_.CreateInsetClip());

  fallback_brush_ = compositor_.CreateColorBrush();
  fallback_ = compositor_.CreateSpriteVisual();
  fallback_.Brush(fallback_brush_);
  fallback_.RelativeSizeAdjustment({1, 1});
  fallback_.Opacity(t.line_opacity);
  fill_host_.Children().InsertAtTop(fallback_);

  try {
    fill_brush_ = compositor_.CreateLinearGradientBrush();
    fill_brush_.MappingMode(composition::CompositionMappingMode::Absolute);
    Bind(fill_brush_, L"StartPoint", L"Vector2(0, host.Size.Y)");
    Bind(fill_brush_, L"EndPoint",
         L"Vector2(Max(m.Bar, host.Size.X * Clamp(m.Progress, 0, 1)), "
         L"host.Size.Y)");
    for (auto& stop : fill_stops_) {
      stop = compositor_.CreateColorGradientStop();
      fill_brush_.ColorStops().Append(stop);
    }
    fill_stops_[1].Offset(1);

    shimmer_brush_ = compositor_.CreateLinearGradientBrush();
    shimmer_brush_.MappingMode(composition::CompositionMappingMode::Relative);
    for (std::size_t i = 0; i < shimmer_stops_.size(); ++i) {
      shimmer_stops_[i] = compositor_.CreateColorGradientStop();
      shimmer_stops_[i].Offset(t.wave_gradient_positions[i]);
      shimmer_brush_.ColorStops().Append(shimmer_stops_[i]);
    }
    Bind(shimmer_brush_, L"StartPoint",
         L"Vector2(m.WaveStart + (m.WaveEnd - m.WaveStart) * m.Shimmer, 0.5)");
    Bind(shimmer_brush_, L"EndPoint",
         L"Vector2(m.WaveStart + (m.WaveEnd - m.WaveStart) * m.Shimmer + "
         L"m.WaveWidth, 0.5)");

    // One recovered radial falloff shapes both full-capsule light layers.
    radial_mask_ = compositor_.CreateRadialGradientBrush();
    radial_mask_.MappingMode(composition::CompositionMappingMode::Absolute);
    for (std::size_t i = 0; i < t.mask_gradient_positions.size(); ++i)
      radial_mask_.ColorStops().Append(compositor_.CreateColorGradientStop(
          t.mask_gradient_positions[i],
          NativeColor(
              Opacity(Color{t.mask_color}, t.mask_gradient_opacity[i]))));
    Bind(radial_mask_, L"EllipseCenter",
         L"Vector2(host.Size.X * Clamp(m.Progress, 0, 1), host.Size.Y)");
    Bind(radial_mask_, L"EllipseRadius",
         L"Vector2(Max(host.Size.X * Clamp(m.Progress, 0, 1) * m.FrontRadiusX, "
         L"host.Size.Y * m.FrontRadiusY), Max(m.Bar, host.Size.Y))");
    radial_mask_.GradientOriginOffset({0, 0});

    const auto glow_brush = compositor_.CreateMaskBrush();
    glow_brush.Source(fill_brush_);
    glow_brush.Mask(radial_mask_);
    glow_ = compositor_.CreateSpriteVisual();
    glow_.RelativeSizeAdjustment({1, 1});
    glow_.Brush(glow_brush);

    const auto glow_shimmer_brush = compositor_.CreateMaskBrush();
    glow_shimmer_brush.Source(shimmer_brush_);
    glow_shimmer_brush.Mask(radial_mask_);
    glow_shimmer_ = compositor_.CreateSpriteVisual();
    glow_shimmer_.RelativeSizeAdjustment({1, 1});
    glow_shimmer_.Brush(glow_shimmer_brush);

    fill_ = compositor_.CreateSpriteVisual();
    fill_.RelativeSizeAdjustment({1, 1});
    fill_.Brush(fill_brush_);
    fill_.Opacity(t.line_opacity);
    shimmer_ = compositor_.CreateSpriteVisual();
    shimmer_.RelativeSizeAdjustment({1, 1});
    shimmer_.Brush(shimmer_brush_);
    shimmer_.Opacity(t.line_opacity);
    for (const auto& part : {glow_, glow_shimmer_, fill_, shimmer_})
      part.IsHitTestVisible(false);

    // The soft light is clipped only by the outer capsule. Only the edge
    // copies have progress-width bounds; a second progress clip cuts the glow.
    visual_.Children().InsertAtTop(glow_);
    visual_.Children().InsertAtTop(glow_shimmer_);
    fill_host_.Children().InsertAtTop(fill_);
    fill_host_.Children().InsertAtTop(shimmer_);
    gradients_available_ = true;
  } catch (...) {
    for (const auto& part : {glow_, glow_shimmer_, fill_, shimmer_})
      if (part)
        part.IsVisible(false);
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Address loading uses the solid accent fallback");
  }
  visual_.Children().InsertAtTop(fill_host_);

  const auto& progress = t.progress_bezier;
  progress_easing_ = compositor_.CreateCubicBezierEasingFunction(
      {progress[0], progress[1]}, {progress[2], progress[3]});
  const auto& fade = t.fade_bezier;
  fade_easing_ = compositor_.CreateCubicBezierEasingFunction(
      {fade[0], fade[1]}, {fade[2], fade[3]});
  progress_animation_ = compositor_.CreateScalarKeyFrameAnimation();
  progress_animation_.InsertExpressionKeyFrame(0, L"this.StartingValue");
  progress_animation_.InsertKeyFrame(1, 1, progress_easing_);
  progress_animation_.Duration(Duration(t.progress_duration_ms));
  progress_animation_.StopBehavior(
      composition::AnimationStopBehavior::SetToFinalValue);
  fade_animation_ = compositor_.CreateScalarKeyFrameAnimation();
  fade_animation_.InsertExpressionKeyFrame(0, L"this.StartingValue");
  fade_animation_.InsertKeyFrame(1, 1, fade_easing_);
  fade_animation_.Duration(Duration(t.fade_duration_ms));
  fade_animation_.StopBehavior(
      composition::AnimationStopBehavior::SetToFinalValue);
  shimmer_animation_ = compositor_.CreateScalarKeyFrameAnimation();
  shimmer_animation_.InsertKeyFrame(0, 0);
  shimmer_animation_.InsertKeyFrame(1, 1,
                                    compositor_.CreateLinearEasingFunction());
  shimmer_animation_.Duration(Duration(t.shimmer_duration_ms));
  shimmer_animation_.IterationBehavior(
      composition::AnimationIterationBehavior::Forever);
  hosting::ElementCompositionPreview::SetElementChildVisual(root_, visual_);
}

void AddressProgress::Palette(const ShellViewModel::LoadingProgressVm& vm,
                              bool high_contrast) {
  const auto& t = kTokens.loading_progress;
  const winrt::Windows::UI::ViewManagement::UISettings settings;
  animations_ = settings.AnimationsEnabled() && !high_contrast;
  const auto accent = settings.GetColorValue(
      winrt::Windows::UI::ViewManagement::UIColorType::Accent);
  Color primary = vm.primary;
  if (high_contrast || !primary.a())
    primary = Color::FromArgb(accent.A, accent.R, accent.G, accent.B);
  primary = primary.WithAlpha(255);
  fallback_brush_.Color(NativeColor(primary));
  fallback_.Opacity(high_contrast ? 1.0f : t.line_opacity);
  const bool gradient = gradients_available_ && !high_contrast;
  fallback_.IsVisible(!gradient);
  if (gradients_available_) {
    fill_.IsVisible(gradient);
    shimmer_.IsVisible(gradient && animations_);
    glow_.IsVisible(gradient);
    glow_shimmer_.IsVisible(gradient && animations_);
    const Color foreground = vm.foreground.a() ? vm.foreground : primary;
    const Color secondary = vm.secondary.a()
                                ? vm.secondary.WithAlpha(255)
                                : Mix(primary, foreground, t.secondary_mix);
    auto leading = compositor_.CreateExpressionAnimation(
        L"ColorLerp(primary, secondary, Clamp(m.Progress, 0, 1))");
    leading.SetColorParameter(L"primary", NativeColor(primary));
    leading.SetColorParameter(L"secondary", NativeColor(secondary));
    leading.SetReferenceParameter(L"m", metrics_);
    fill_stops_[0].StartAnimation(L"Color", leading);
    fill_stops_[1].Color(NativeColor(primary));
    for (std::size_t i = 0; i < shimmer_stops_.size(); ++i)
      shimmer_stops_[i].Color(
          NativeColor(Opacity(foreground, t.wave_gradient_opacity[i])));
  }
  if (!animations_) {
    Stop();
    visual_.Opacity(1);
  }
  primary_ = vm.primary;
  secondary_ = vm.secondary;
  foreground_ = vm.foreground;
  palette_ready_ = true;
}

void AddressProgress::ResetBatch(Completion& completion) noexcept {
  if (const auto batch = std::exchange(completion.batch, nullptr)) {
    try {
      batch.Completed(std::exchange(completion.token, {}));
    } catch (...) {
      platform::WriteDefaultLog(LogLevel::kWarning,
                                "Address loading completion could not detach");
    }
  }
}

void AddressProgress::AnimateProgress(float target) {
  pending_progress_ = std::max(pending_progress_, target);
  if (!animations_) {
    target_ = pending_progress_;
    metrics_.InsertScalar(L"Progress", target_);
    return;
  }
  // One in-flight transition and one latest target, never a backlog of steps.
  if (progress_running_ || pending_progress_ <= target_)
    return;
  target_ = pending_progress_;
  progress_running_ = true;
  const auto generation = animation_generation_;
  const std::weak_ptr<int> weak = lifetime_;
  ResetBatch(progress_completion_);
  progress_completion_.batch = compositor_.CreateScopedBatch(
      composition::CompositionBatchTypes::Animation);
  progress_completion_.token = progress_completion_.batch.Completed(
      [this, weak, generation](auto&&, auto&&) {
        if (weak.expired() || generation != animation_generation_)
          return;
        try {
          ResetBatch(progress_completion_);
          progress_running_ = false;
          if (pending_progress_ > target_)
            AnimateProgress(pending_progress_);
          else if (finishing_)
            Exit();
        } catch (...) {
          Hide();
          platform::WriteDefaultLog(
              LogLevel::kError, "Address loading progress could not finish");
        }
      });
  progress_animation_.InsertKeyFrame(1, target_, progress_easing_);
  metrics_.StartAnimation(L"Progress", progress_animation_);
  progress_completion_.batch.End();
}

void AddressProgress::Shimmer(bool active) {
  active = active && animations_ && gradients_available_;
  if (shimmering_ == active)
    return;
  shimmering_ = active;
  if (active)
    metrics_.StartAnimation(L"Shimmer", shimmer_animation_);
  else
    metrics_.StopAnimation(L"Shimmer");
}

void AddressProgress::Fade(float opacity) {
  if (!animations_) {
    visual_.StopAnimation(L"Opacity");
    visual_.Opacity(opacity);
    return;
  }
  fade_animation_.InsertKeyFrame(1, opacity, fade_easing_);
  visual_.StartAnimation(L"Opacity", fade_animation_);
}

void AddressProgress::Stop() {
  ++animation_generation_;
  ResetBatch(progress_completion_);
  ResetBatch(fade_completion_);
  if (!metrics_)
    return;
  metrics_.StopAnimation(L"Progress");
  if (visual_)
    visual_.StopAnimation(L"Opacity");
  Shimmer(false);
  progress_running_ = false;
  finishing_ = false;
}

void AddressProgress::Hide() noexcept {
  try {
    Stop();
    root_.Visibility(xaml::Visibility::Collapsed);
    visible_ = false;
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kError,
                              "Address loading could not reset");
  }
}

void AddressProgress::Finish() {
  finishing_ = true;
  AnimateProgress(1);
  if (!progress_running_)
    Exit();
}

void AddressProgress::Exit() {
  ResetBatch(fade_completion_);
  const auto generation = animation_generation_;
  const std::weak_ptr<int> weak = lifetime_;
  fade_completion_.batch = compositor_.CreateScopedBatch(
      composition::CompositionBatchTypes::Animation);
  fade_completion_.token = fade_completion_.batch.Completed(
      [this, weak, generation](auto&&, auto&&) {
        if (weak.expired() || generation != animation_generation_)
          return;
        try {
          Hide();
          ended_ = true;
        } catch (...) {
          platform::WriteDefaultLog(LogLevel::kError,
                                    "Address loading could not hide");
        }
      });
  Fade(0);
  fade_completion_.batch.End();
}

void AddressProgress::Render(const ShellViewModel::LoadingProgressVm& vm,
                             bool rtl,
                             bool high_contrast,
                             bool refresh) {
  if (refresh || contrast_ != high_contrast || primary_ != vm.primary ||
      secondary_ != vm.secondary || foreground_ != vm.foreground)
    palette_ready_ = false;
  if (last_ && *last_ == vm.state && requested_ == vm.visible && rtl_ == rtl &&
      contrast_ == high_contrast && palette_ready_)
    return;
  const bool new_session = !last_ || last_->tab != vm.state.tab ||
                           last_->session != vm.state.session;
  const bool palette_changed = !palette_ready_ || new_session || !visible_;
  last_ = vm.state;
  requested_ = vm.visible;
  rtl_ = rtl;
  contrast_ = high_contrast;
  if (new_session) {
    Hide();
    ended_ = false;
  }
  if (!vm.visible || vm.state.phase == LoadPhase::kIdle) {
    Hide();
    ended_ = vm.state.phase == LoadPhase::kComplete;
    return;
  }
  if (vm.state.phase == LoadPhase::kComplete && (ended_ || !visible_)) {
    ended_ = true;
    return;
  }
  if (!Ensure())
    return;
  if (palette_changed)
    Palette(vm, high_contrast);
  visual_.Scale({rtl ? -1.0f : 1.0f, 1, 1});
  if (vm.state.phase == LoadPhase::kLoading) {
    if (!visible_ || new_session || finishing_) {
      Stop();
      target_ = pending_progress_ = new_session ? 0 : vm.state.progress;
      metrics_.InsertScalar(L"Progress", target_);
      visual_.Opacity(0);
      root_.Visibility(xaml::Visibility::Visible);
      visible_ = true;
      ended_ = false;
      Fade(1);
    }
    AnimateProgress(vm.state.progress);
    Shimmer(true);
  } else if (!animations_) {
    Hide();
    ended_ = true;
  } else if (!finishing_) {
    Finish();
  }
}

}  // namespace kr::win
