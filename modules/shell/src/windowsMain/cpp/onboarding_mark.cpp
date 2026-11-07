#include "onboarding_mark.hpp"

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <cmath>

#include "native_motion.hpp"

namespace kr::win {
namespace composition = winrt::Microsoft::UI::Composition;
using winrt::Windows::Foundation::Numerics::float2;

namespace {
const auto& Mark() noexcept {
  return kTokens.onboarding.mark;
}
// Source viewBox units to DIPs inside the mark's square.
float2 Map(float x, float y) noexcept {
  const float scale = Mark().size / Mark().view_size;
  return {(x - Mark().view_x) * scale, (y - Mark().view_y) * scale};
}
}  // namespace

OnboardingMark::OnboardingMark() {
  root_.Width(Mark().size);
  root_.Height(Mark().size);
  root_.IsHitTestVisible(false);
  // Decorative: the title beside it names the product.
  xaml::Automation::AutomationProperties::SetAccessibilityView(
      root_, xaml::Automation::Peers::AccessibilityView::Raw);
}

void OnboardingMark::Build() {
  if (visual_)
    return;
  const auto compositor =
      xaml::Media::CompositionTarget::GetCompositorForCurrentThread();
  visual_ = compositor.CreateShapeVisual();
  visual_.Size({Mark().size, Mark().size});
  visual_.CenterPoint({Mark().size / 2, Mark().size / 2, 0});
  tilt_ = compositor.CreateSpringScalarAnimation();
  Configure(tilt_, {kTokens.onboarding.point_spring_damping_ratio,
                    kTokens.onboarding.point_spring_period_ms});
  const float scale = Mark().size / Mark().view_size;
  const auto backplate =
      compositor.CreateColorBrush(NativeColor(Color{Mark().backplate}));
  for (std::size_t pass = 0; pass < 2; ++pass) {
    for (std::size_t i = 0; i < Mark().strokes.size(); ++i) {
      const auto& stroke = Mark().strokes[i];
      auto line = compositor.CreateLineGeometry();
      line.Start(Map(stroke.x1, stroke.y1));
      line.End(Map(stroke.x2, stroke.y2));
      line.TrimEnd(0);
      auto shape = compositor.CreateSpriteShape(line);
      shape.StrokeStartCap(composition::CompositionStrokeCap::Round);
      shape.StrokeEndCap(composition::CompositionStrokeCap::Round);
      if (pass == 0) {
        shape.StrokeThickness(Mark().backplate_width * scale);
        shape.StrokeBrush(backplate);
      } else {
        auto gradient = compositor.CreateLinearGradientBrush();
        gradient.MappingMode(composition::CompositionMappingMode::Absolute);
        gradient.StartPoint(Map(stroke.gx1, stroke.gy1));
        gradient.EndPoint(Map(stroke.gx2, stroke.gy2));
        for (std::size_t s = 0; s < stroke.offsets.size(); ++s)
          gradient.ColorStops().Append(compositor.CreateColorGradientStop(
              stroke.offsets[s], NativeColor(Color{stroke.colors[s]})));
        shape.StrokeThickness(Mark().stroke_width * scale);
        shape.StrokeBrush(gradient);
      }
      visual_.Shapes().Append(shape);
      lines_[pass * Mark().strokes.size() + i] = line;
    }
  }
  xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(root_,
                                                                  visual_);
  draw_ = compositor.CreateScalarKeyFrameAnimation();
  draw_.InsertKeyFrame(0, 0);
  draw_.InsertKeyFrame(1, 1, Bezier(compositor, kTokens.onboarding.ease_out));
  draw_.Duration(Ms(kTokens.onboarding.card_fade_ms));
  draw_.DelayBehavior(
      composition::AnimationDelayBehavior::SetInitialValueBeforeDelay);
}

void OnboardingMark::Draw(bool animate) {
  Build();
  const std::size_t count = Mark().strokes.size();
  for (std::size_t i = 0; i < lines_.size(); ++i) {
    auto& line = lines_[i];
    line.StopAnimation(L"TrimEnd");
    if (!animate) {
      line.TrimEnd(1);
      continue;
    }
    draw_.DelayTime(Ms(Mark().draw_stagger_ms * static_cast<float>(i % count)));
    line.StartAnimation(L"TrimEnd", draw_);
  }
}

void OnboardingMark::Reset() noexcept {
  for (auto& line : lines_) {
    if (!line)
      continue;
    try {
      line.StopAnimation(L"TrimEnd");
      line.TrimEnd(0);
    } catch (...) {
      // The island may already have released its compositor resources.
    }
  }
}

void OnboardingMark::Tilt(Point point, bool animate) {
  if (!visual_)
    return;
  visual_.StopAnimation(L"RotationAngleInDegrees");
  if (!animate) {
    visual_.RotationAngleInDegrees(0);
    return;
  }
  const float x = std::clamp(point.x, -1.0f, 1.0f);
  const float y = std::clamp(point.y, -1.0f, 1.0f);
  const float distance = std::hypot(x, y);
  visual_.RotationAxis(
      distance > 0
          ? winrt::Windows::Foundation::Numerics::float3{-y / distance,
                                                         x / distance, 0}
          : winrt::Windows::Foundation::Numerics::float3{0, 1, 0});
  tilt_.FinalValue(std::min(distance, 1.0f) * Mark().tilt_degrees);
  visual_.StartAnimation(L"RotationAngleInDegrees", tilt_);
}
}  // namespace kr::win
