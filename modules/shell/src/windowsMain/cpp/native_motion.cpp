#include "native_motion.hpp"

#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
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

EdgeShimmer::~EdgeShimmer() {
  Stop();
  try {
    if (visual_ && host_)
      xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(host_,
                                                                      nullptr);
  } catch (...) {
    // A closed native island may already have released its composition tree.
  }
}

void EdgeShimmer::Ensure() {
  if (visual_)
    return;
  constexpr auto& t = kTokens.overlay;
  static_assert(t.shimmer_falloff_positions.size() ==
                t.shimmer_falloff_opacity.size());
  // Built locally and committed together: a failure leaves nothing partial.
  const auto host =
      xaml::Hosting::ElementCompositionPreview::GetElementVisual(host_);
  const auto compositor = host.Compositor();
  auto metrics = compositor.CreatePropertySet();
  metrics.InsertScalar(L"Progress", 0);
  // Distance<i> along the edge per wave; Head<i> and Vertical<i> derive from
  // it in a separate set, so no expression reads its own target.
  auto path = compositor.CreatePropertySet();
  auto frame = compositor.CreatePropertySet();
  struct Reference {
    const wchar_t* name;
    composition::CompositionObject object;
  };
  struct Scalar {
    const wchar_t* name;
    float value;
  };
  const auto bind = [&](const composition::CompositionObject& target,
                        std::wstring_view property, const std::wstring& text,
                        std::initializer_list<Reference> references,
                        std::initializer_list<Scalar> scalars = {}) {
    auto expression =
        compositor.CreateExpressionAnimation(winrt::hstring{text});
    for (const auto& reference : references)
      expression.SetReferenceParameter(reference.name, reference.object);
    for (const auto& scalar : scalars)
      expression.SetScalarParameter(scalar.name, scalar.value);
    target.StartAnimation(winrt::hstring{property}, expression);
  };
  // Distances run clockwise in DIPs from the top-left corner and stay within
  // two laps, so positions are plain clamped segments for each lap. The top
  // edge ends at W, the right at W + H, the bottom at 2W + H, the left at a
  // full lap; corners are followed square, which the soft light hides.
  const std::wstring w = L"host.Size.X", h = L"host.Size.Y";
  const std::wstring lap = L"(2 * (" + w + L" + " + h + L"))";
  const std::array<std::wstring, 4> ends{w, L"(" + w + L" + " + h + L")",
                                         L"(2 * " + w + L" + " + h + L")", lap};
  const auto segment = [](const std::wstring& base, const std::wstring& extent,
                          const std::wstring& start, const std::wstring& end) {
    return L"Clamp(" + base + L" - " + start + L", 0, " + extent +
           L") - Clamp(" + base + L" - " + end + L", 0, " + extent + L")";
  };
  // 0 to 1 across [at - Blend, at + Blend].
  const auto ramp = [](const std::wstring& base, const std::wstring& at) {
    return L"Clamp((" + base + L" - " + at + L") / (2 * Blend) + 0.5, 0, 1)";
  };
  auto visual = compositor.CreateContainerVisual();
  // The parent XAML gives a child visual need not have a size, so the root
  // follows the element's own composition size; the layers fill the root.
  bind(visual, L"Size", L"host.Size", {{L"host", host}});
  visual.Opacity(0);
  // Sheens inside the rounded edge. Clip corner radii cannot be animated;
  // Play sets them with the ring's.
  auto glows = compositor.CreateContainerVisual();
  glows.RelativeSizeAdjustment({1, 1});
  auto clip = compositor.CreateRectangleClip();
  bind(clip, L"Right", w, {{L"host", host}});
  bind(clip, L"Bottom", h, {{L"host", host}});
  glows.Clip(clip);
  // The edge line, one stroke per wave over the same rounded path.
  const float inset = t.shimmer_line_width / 2;
  auto ring = compositor.CreateRoundedRectangleGeometry();
  ring.Offset({inset, inset});
  bind(ring, L"Size",
       L"Vector2(Max(0, " + w + L" - 2 * Inset), Max(0, " + h +
           L" - 2 * Inset))",
       {{L"host", host}}, {{L"Inset", inset}});
  auto line = compositor.CreateShapeVisual();
  line.RelativeSizeAdjustment({1, 1});
  // Leading wave on top: build from the last (trailing) wave forward.
  for (std::size_t wave = t.shimmer_wave_colors.size(); wave-- > 0;) {
    const std::wstring n = std::to_wstring(wave);
    const std::wstring distance = L"Distance" + n;
    path.InsertScalar(winrt::hstring{distance}, 0);
    bind(path, distance,
         w + L" + " + h + L" / 2 + (Lead + m.Progress) * " + lap,
         {{L"host", host}, {L"m", metrics}},
         {{L"Lead", t.shimmer_start_offset -
                        t.shimmer_wave_gap * static_cast<float>(wave)}});
    const std::wstring s = L"p." + distance;
    const std::wstring next = L"(" + s + L" - " + lap + L")";
    frame.InsertVector2(winrt::hstring{L"Head" + n}, {0, 0});
    bind(frame, L"Head" + n,
         L"Vector2(" + segment(s, w, L"0", ends[1]) + L" + " +
             segment(next, w, L"0", ends[1]) + L", " +
             segment(s, h, ends[0], ends[2]) + L" + " +
             segment(next, h, ends[0], ends[2]) + L")",
         {{L"host", host}, {L"p", path}});
    // 1 along the right and left edges, 0 along the top and bottom.
    std::wstring vertical;
    for (const auto& base : {s, next})
      vertical += (vertical.empty() ? L"" : L" + ") + ramp(base, ends[0]) +
                  L" - " + ramp(base, ends[1]) + L" + " + ramp(base, ends[2]) +
                  L" - " + ramp(base, ends[3]);
    frame.InsertScalar(winrt::hstring{L"Vertical" + n}, 0);
    bind(frame, L"Vertical" + n, vertical, {{L"host", host}, {L"p", path}},
         {{L"Blend", t.shimmer_corner_blend}});
    // A long, shallow sheen along the edge the head is on, turning with it
    // at corners, with an eased falloff so no ring or band edge shows.
    const auto sheen = [&](float opacity, float along, float across) {
      auto brush = compositor.CreateRadialGradientBrush();
      brush.MappingMode(composition::CompositionMappingMode::Absolute);
      brush.GradientOriginOffset({0, 0});
      const Color color{t.shimmer_wave_colors[wave]};
      for (std::size_t i = 0; i < t.shimmer_falloff_positions.size(); ++i)
        brush.ColorStops().Append(compositor.CreateColorGradientStop(
            t.shimmer_falloff_positions[i],
            NativeColor(color.WithAlpha(static_cast<std::uint8_t>(
                std::lround(static_cast<float>(color.a()) * opacity *
                            t.shimmer_falloff_opacity[i]))))));
      const std::wstring v = L"f.Vertical" + n;
      const std::wstring reach = L"Min(" + w + L" * Share, Along)";
      bind(brush, L"EllipseCenter", L"f.Head" + n, {{L"f", frame}});
      bind(brush, L"EllipseRadius",
           L"Vector2(" + reach + L" * (1 - " + v + L") + Across * " + v +
               L", " + reach + L" * " + v + L" + Across * (1 - " + v + L"))",
           {{L"host", host}, {L"f", frame}},
           {{L"Share", t.shimmer_share},
            {L"Along", along},
            {L"Across", across}});
      return brush;
    };
    auto glow = compositor.CreateSpriteVisual();
    glow.RelativeSizeAdjustment({1, 1});
    glow.Brush(sheen(t.shimmer_glow_opacity, t.shimmer_glow_along,
                     t.shimmer_glow_across));
    glows.Children().InsertAtTop(glow);
    auto stroke = compositor.CreateSpriteShape(ring);
    stroke.StrokeThickness(t.shimmer_line_width);
    stroke.StrokeBrush(sheen(1, t.shimmer_line_along, t.shimmer_line_across));
    line.Shapes().Append(stroke);
  }
  visual.Children().InsertAtTop(glows);
  visual.Children().InsertAtTop(line);

  const float duration = t.shimmer_duration_ms;
  const auto at = [duration](float milliseconds) {
    return std::clamp(milliseconds / duration, 0.0f, 1.0f);
  };
  const auto ease_in = Bezier(compositor, t.shimmer_ease_in);
  const auto ease_out = Bezier(compositor, t.shimmer_ease_out);
  auto sweep = compositor.CreateScalarKeyFrameAnimation();
  sweep.InsertKeyFrame(0, 0);
  sweep.InsertKeyFrame(at(t.shimmer_midpoint_ms), t.shimmer_turn / 2, ease_in);
  sweep.InsertKeyFrame(at(t.shimmer_sweep_ms), t.shimmer_turn, ease_out);
  sweep.InsertKeyFrame(1, t.shimmer_turn);
  sweep.Duration(Ms(duration));
  auto fade = compositor.CreateScalarKeyFrameAnimation();
  fade.InsertKeyFrame(0, 0);
  fade.InsertKeyFrame(at(t.shimmer_fade_in_ms), 1, ease_out);
  fade.InsertKeyFrame(at(t.shimmer_fade_out_start_ms), 1);
  fade.InsertKeyFrame(1, 0, ease_in);
  fade.Duration(Ms(duration));
  fade.StopBehavior(composition::AnimationStopBehavior::SetToFinalValue);
  xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(host_,
                                                                  visual);
  visual_ = std::move(visual);
  clip_ = std::move(clip);
  ring_ = std::move(ring);
  metrics_ = std::move(metrics);
  path_ = std::move(path);
  frame_ = std::move(frame);
  sweep_ = std::move(sweep);
  fade_ = std::move(fade);
}

void EdgeShimmer::Play(float radius,
                       const NativeMotion& motion,
                       bool high_contrast) noexcept {
  Stop();
  if (!host_ || !motion.Enabled(high_contrast) ||
      !(kTokens.overlay.shimmer_duration_ms > 0))
    return;
  try {
    Ensure();
    // Corner radii are small next to the bar, so no size clamp is needed.
    const float outer = std::max(0.0f, radius);
    const float inner =
        std::max(0.0f, outer - kTokens.overlay.shimmer_line_width / 2);
    clip_.TopLeftRadius({outer, outer});
    clip_.TopRightRadius({outer, outer});
    clip_.BottomLeftRadius({outer, outer});
    clip_.BottomRightRadius({outer, outer});
    ring_.CornerRadius({inner, inner});
    started_ = true;
    metrics_.StartAnimation(L"Progress", sweep_);
    visual_.StartAnimation(L"Opacity", fade_);
  } catch (const winrt::hresult_error& error) {
    Stop();
    std::array<char, 256> message{};
    std::snprintf(message.data(), message.size(),
                  "Edge shimmer could not start: 0x%08X %s",
                  static_cast<unsigned int>(error.code().value),
                  winrt::to_string(error.message()).c_str());
    platform::WriteDefaultLog(LogLevel::kWarning, message.data());
  } catch (...) {
    Stop();
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Edge shimmer could not start");
  }
}

void EdgeShimmer::Stop() noexcept {
  if (!std::exchange(started_, false))
    return;
  try {
    metrics_.StopAnimation(L"Progress");
    visual_.StopAnimation(L"Opacity");
    visual_.Opacity(0);
  } catch (...) {
    // A closed native island may already have released its composition tree.
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
