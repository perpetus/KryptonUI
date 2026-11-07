#include "native_motion.hpp"

#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.Foundation.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string_view>
#include <utility>

#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
namespace composition = winrt::Microsoft::UI::Composition;

NativeMotion::NativeMotion() noexcept {
  if (!kTokens.motion.enabled)
    return;
  std::string_view stage = "Motion system settings unavailable";
  try {
    settings_ = winrt::Windows::UI::ViewManagement::UISettings();
    accessibility_ =
        winrt::Windows::UI::ViewManagement::AccessibilitySettings();
    preferences_ = std::make_shared<Preferences>();
    preferences_->animations.store(settings_.AnimationsEnabled(),
                                   std::memory_order_relaxed);
    preferences_->contrast.store(accessibility_.HighContrast(),
                                 std::memory_order_relaxed);
    const std::weak_ptr<Preferences> weak = preferences_;
    stage = "Motion animation notification unavailable";
    changed_ =
        settings_.AnimationsEnabledChanged([weak](auto&& sender, auto&&) {
          if (const auto preferences = weak.lock())
            preferences->animations.store(sender.AnimationsEnabled(),
                                          std::memory_order_relaxed);
        });
    listening_ = true;
    // Some desktop hosts do not support HighContrastChanged. The native
    // WM_SETTINGCHANGE message refreshes this preference instead.
  } catch (...) {
    // Motion is optional. A failed settings query or subscription must never
    // turn into a shell-creation failure.
    Reset();
    platform::WriteDefaultLog(LogLevel::kWarning, stage);
  }
}

NativeMotion::~NativeMotion() {
  Reset();
}

void NativeMotion::Reset() noexcept {
  preferences_.reset();
  if (std::exchange(listening_, false))
    settings_.AnimationsEnabledChanged(std::exchange(changed_, {}));
}

void NativeMotion::RefreshContrast() noexcept {
  if (!preferences_)
    return;
  try {
    preferences_->contrast.store(accessibility_.HighContrast(),
                                 std::memory_order_relaxed);
  } catch (...) {
    preferences_->contrast.store(true, std::memory_order_relaxed);
  }
}

bool NativeMotion::Enabled(bool high_contrast) const noexcept {
  return kTokens.motion.enabled && preferences_ && !high_contrast &&
         preferences_->animations.load(std::memory_order_relaxed) &&
         !preferences_->contrast.load(std::memory_order_relaxed);
}

void NativeMotion::PrepareBackground(const controls::Border& target,
                                     bool high_contrast) {
  const bool animate = Enabled(high_contrast) && kTokens.motion.fast_ms > 0;
  if (animate && !hover_) {
    hover_ = xaml::BrushTransition();
    hover_.Duration(
        std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
            std::chrono::duration<float, std::milli>(kTokens.motion.fast_ms)));
  }
  const auto transition = animate ? hover_ : nullptr;
  if (target.BackgroundTransition() != transition)
    target.BackgroundTransition(transition);
}

void ContentFade::Play(const xaml::UIElement& target,
                       const NativeMotion& motion,
                       bool high_contrast,
                       float milliseconds) noexcept {
  Stop();
  if (!target || !motion.Enabled(high_contrast) ||
      !std::isfinite(milliseconds) || milliseconds <= 0)
    return;
  try {
    const auto visual =
        xaml::Hosting::ElementCompositionPreview::GetElementVisual(target);
    if (visual_ != visual) {
      visual_ = visual;
      fade_ = nullptr;
    }
    if (!fade_) {
      const auto compositor = visual_.Compositor();
      fade_ = compositor.CreateScalarKeyFrameAnimation();
      const auto easing = compositor.CreateLinearEasingFunction();
      fade_.InsertKeyFrame(0, 0, easing);
      fade_.InsertKeyFrame(1, 1, easing);
      fade_.StopBehavior(composition::AnimationStopBehavior::SetToFinalValue);
    }
    fade_.Duration(
        std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
            std::chrono::duration<float, std::milli>(milliseconds)));
    started_ = true;
    visual_.StartAnimation(L"Opacity", fade_);
  } catch (...) {
    Stop();
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Content fade could not start");
  }
}

std::chrono::milliseconds Ms(float milliseconds) noexcept {
  return std::chrono::milliseconds(static_cast<std::int64_t>(
      std::isfinite(milliseconds) ? std::max(0.0f, milliseconds) : 0.0f));
}

composition::Compositor CompositorFor(const xaml::UIElement& element) {
  return xaml::Hosting::ElementCompositionPreview::GetElementVisual(element)
      .Compositor();
}

composition::CubicBezierEasingFunction Bezier(
    const composition::Compositor& compositor,
    const std::array<float, 4>& curve) {
  return compositor.CreateCubicBezierEasingFunction({curve[0], curve[1]},
                                                    {curve[2], curve[3]});
}

namespace {
template <typename Spring>
void ConfigureSpring(const Spring& spring, SpringSpec spec) {
  spring.DampingRatio(spec.damping_ratio);
  spring.Period(
      std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
          std::chrono::duration<float, std::milli>(spec.period_ms)));
}
}  // namespace

void Configure(const composition::SpringVector3NaturalMotionAnimation& spring,
               SpringSpec spec) {
  ConfigureSpring(spring, spec);
}
void Configure(const composition::SpringScalarNaturalMotionAnimation& spring,
               SpringSpec spec) {
  ConfigureSpring(spring, spec);
}
void Configure(const composition::SpringVector2NaturalMotionAnimation& spring,
               SpringSpec spec) {
  ConfigureSpring(spring, spec);
}

void Choreography::Ensure(const composition::Compositor& compositor) {
  if (move_)
    return;
  move_ = compositor.CreateSpringVector3Animation();
  move_.Target(L"Translation");
  move_.FinalValue(winrt::Windows::Foundation::Numerics::float3{0, 0, 0});
  move_.DelayBehavior(
      composition::AnimationDelayBehavior::SetInitialValueBeforeDelay);
  show_ = compositor.CreateSpringScalarAnimation();
  show_.Target(L"Opacity");
  show_.InitialValue(0.0f);
  show_.FinalValue(1.0f);
  show_.DelayBehavior(
      composition::AnimationDelayBehavior::SetInitialValueBeforeDelay);
  leave_ = compositor.CreateVector3KeyFrameAnimation();
  leave_.Target(L"Translation");
  hide_ = compositor.CreateScalarKeyFrameAnimation();
  hide_.Target(L"Opacity");
}

void Choreography::Rise(std::span<const xaml::UIElement> elements,
                        const RiseSpec& spec) {
  if (elements.empty())
    return;
  Ensure(CompositorFor(elements.front()));
  Configure(move_, spec.spring);
  Configure(show_, spec.spring);
  move_.InitialValue(spec.from);
  for (std::size_t i = 0; i < elements.size(); ++i) {
    const auto delay =
        Ms(spec.delay_ms + spec.stagger_ms * static_cast<float>(i));
    move_.DelayTime(delay);
    elements[i].StartAnimation(move_);
    if (spec.fade) {
      show_.DelayTime(delay);
      elements[i].StartAnimation(show_);
    } else {
      elements[i].Opacity(1);
    }
  }
}

void Choreography::Recede(std::span<const xaml::UIElement> elements,
                          const RecedeSpec& spec) {
  if (elements.empty())
    return;
  const auto compositor = CompositorFor(elements.front());
  Ensure(compositor);
  const auto easing = Bezier(compositor, spec.easing);
  leave_.InsertExpressionKeyFrame(0, L"this.StartingValue");
  leave_.InsertKeyFrame(1, spec.to, easing);
  hide_.InsertExpressionKeyFrame(0, L"this.StartingValue");
  hide_.InsertKeyFrame(1, 0, easing);
  leave_.Duration(Ms(spec.duration_ms));
  hide_.Duration(Ms(spec.duration_ms));
  leave_.DelayBehavior(
      composition::AnimationDelayBehavior::SetInitialValueAfterDelay);
  hide_.DelayBehavior(
      composition::AnimationDelayBehavior::SetInitialValueAfterDelay);
  for (std::size_t i = 0; i < elements.size(); ++i) {
    const auto delay =
        Ms(spec.delay_ms + spec.stagger_ms * static_cast<float>(i));
    leave_.DelayTime(delay);
    hide_.DelayTime(delay);
    elements[i].StartAnimation(leave_);
    elements[i].StartAnimation(hide_);
  }
}

void Choreography::Snap(std::span<const xaml::UIElement> elements,
                        bool visible) noexcept {
  for (const auto& element : elements) {
    try {
      element.Translation({0, 0, 0});
      element.Opacity(visible ? 1.0 : 0.0);
    } catch (...) {
      // A closed island may already have released the element's visual.
    }
  }
}

void MotionBatch::Begin(const composition::Compositor& compositor) {
  Cancel();
  batch_ = compositor.CreateScopedBatch(
      composition::CompositionBatchTypes::Animation);
}

void MotionBatch::End(Task done) {
  if (!batch_)
    return;
  done_ = std::move(done);
  const auto serial = ++serial_;
  const std::weak_ptr<int> weak = lifetime_;
  // Retain the event source explicitly: these compositor objects do not
  // support the weak-reference interface required by auto_revoke.
  const auto batch = batch_;
  completed_ = batch.Completed([this, weak, serial](auto&&, auto&&) {
    if (weak.expired() || serial != serial_)
      return;
    batch_.Completed(completed_);
    listening_ = false;
    batch_ = nullptr;
    if (auto done = std::exchange(done_, {}))
      done();
  });
  listening_ = true;
  batch.End();
}

void MotionBatch::Cancel() noexcept {
  ++serial_;
  if (std::exchange(listening_, false) && batch_)
    batch_.Completed(completed_);
  done_ = {};
  if (batch_) {
    try {
      batch_.End();
    } catch (...) {
      // An ended batch needs nothing more.
    }
  }
  batch_ = nullptr;
}

void ContentFade::Stop() noexcept {
  if (!std::exchange(started_, false))
    return;
  try {
    if (visual_) {
      visual_.StopAnimation(L"Opacity");
      visual_.Opacity(1);
    }
  } catch (...) {
    // A closed native island may already have released its composition tree.
    visual_ = nullptr;
    fade_ = nullptr;
  }
}
}  // namespace kr::win
