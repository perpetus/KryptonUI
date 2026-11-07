#pragma once

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>

#include "xaml_factory.hpp"

namespace kr::win {
// Native motion policy is push-driven. The system notification may arrive on
// another thread; it updates only this small shared flag, never XAML objects.
class NativeMotion {
 public:
  NativeMotion() noexcept;
  ~NativeMotion();
  NativeMotion(const NativeMotion&) = delete;
  NativeMotion& operator=(const NativeMotion&) = delete;
  [[nodiscard]] bool Enabled(bool high_contrast) const noexcept;
  void RefreshContrast() noexcept;
  void PrepareBackground(const controls::Border& target, bool high_contrast);

 private:
  void Reset() noexcept;
  struct Preferences {
    std::atomic_bool animations{false};
    std::atomic_bool contrast{false};
  };
  winrt::Windows::UI::ViewManagement::UISettings settings_{nullptr};
  winrt::Windows::UI::ViewManagement::AccessibilitySettings accessibility_{
      nullptr};
  std::shared_ptr<Preferences> preferences_;
  // Retain each event source with its explicit token. System settings sources
  // are not required to expose IWeakReferenceSource for auto_revoke.
  winrt::event_token changed_{};
  bool listening_ = false;
  xaml::BrushTransition hover_{nullptr};
};

// One finite compositor animation per retained surface. No completion callback
// or delayed close is needed; geometry, focus and dismissal remain immediate.
class ContentFade {
 public:
  ContentFade() = default;
  ~ContentFade() { Stop(); }
  ContentFade(const ContentFade&) = delete;
  ContentFade& operator=(const ContentFade&) = delete;
  void Play(const xaml::UIElement& target,
            const NativeMotion& motion,
            bool high_contrast,
            float milliseconds = kTokens.motion.fast_ms) noexcept;
  void Stop() noexcept;

 private:
  winrt::Microsoft::UI::Composition::Visual visual_{nullptr};
  winrt::Microsoft::UI::Composition::ScalarKeyFrameAnimation fade_{nullptr};
  bool started_ = false;
};

// Shared compositor helpers. Callers check NativeMotion::Enabled first and
// apply the final state directly when motion is off.
[[nodiscard]] std::chrono::milliseconds Ms(float milliseconds) noexcept;
[[nodiscard]] winrt::Microsoft::UI::Composition::Compositor CompositorFor(
    const xaml::UIElement& element);
[[nodiscard]] winrt::Microsoft::UI::Composition::CubicBezierEasingFunction
Bezier(const winrt::Microsoft::UI::Composition::Compositor& compositor,
       const std::array<float, 4>& curve);
// A spring as a damping ratio and an undamped period, the token convention.
struct SpringSpec {
  float damping_ratio = 1;
  float period_ms = 0;
};
void Configure(const winrt::Microsoft::UI::Composition::
                   SpringVector3NaturalMotionAnimation& spring,
               SpringSpec spec);
void Configure(
    const winrt::Microsoft::UI::Composition::SpringScalarNaturalMotionAnimation&
        spring,
    SpringSpec spec);
void Configure(const winrt::Microsoft::UI::Composition::
                   SpringVector2NaturalMotionAnimation& spring,
               SpringSpec spec);

// Opacity and Translation for a group of elements. Rise starts each element
// displaced and transparent and springs it home; Recede eases it away. Each
// element waits with a delay-before-start, so a stagger needs no timer. The
// four animations are created once and reused: starting one copies it.
class Choreography {
 public:
  struct RiseSpec {
    SpringSpec spring;
    float delay_ms = 0, stagger_ms = 0;
    winrt::Windows::Foundation::Numerics::float3 from{};
    bool fade = true;
  };
  struct RecedeSpec {
    float duration_ms = 0, delay_ms = 0, stagger_ms = 0;
    std::array<float, 4> easing{};
    winrt::Windows::Foundation::Numerics::float3 to{};
  };
  void Rise(std::span<const xaml::UIElement> elements, const RiseSpec& spec);
  void Recede(std::span<const xaml::UIElement> elements,
              const RecedeSpec& spec);
  // At rest and fully shown or hidden: reduced motion and interruption.
  static void Snap(std::span<const xaml::UIElement> elements,
                   bool visible) noexcept;

 private:
  void Ensure(const winrt::Microsoft::UI::Composition::Compositor& compositor);
  winrt::Microsoft::UI::Composition::SpringVector3NaturalMotionAnimation move_{
      nullptr};
  winrt::Microsoft::UI::Composition::SpringScalarNaturalMotionAnimation show_{
      nullptr};
  winrt::Microsoft::UI::Composition::Vector3KeyFrameAnimation leave_{nullptr};
  winrt::Microsoft::UI::Composition::ScalarKeyFrameAnimation hide_{nullptr};
};

// Collects the animations started between Begin and End. The completion runs
// once, on the UI thread, unless a newer batch began or this object is gone.
// A batch without animations completes on the next compositor commit, so the
// reduced-motion path takes the same route.
class MotionBatch {
 public:
  MotionBatch() = default;
  ~MotionBatch() {
    lifetime_.reset();
    Cancel();
  }
  MotionBatch(const MotionBatch&) = delete;
  MotionBatch& operator=(const MotionBatch&) = delete;
  void Begin(const winrt::Microsoft::UI::Composition::Compositor& compositor);
  void End(Task done);
  void Cancel() noexcept;

 private:
  winrt::Microsoft::UI::Composition::CompositionScopedBatch batch_{nullptr};
  winrt::event_token completed_{};
  bool listening_ = false;
  Task done_;
  std::uint64_t serial_ = 0;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
};
}  // namespace kr::win
