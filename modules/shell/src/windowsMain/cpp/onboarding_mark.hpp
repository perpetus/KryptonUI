#pragma once

#include <winrt/Microsoft.UI.Composition.h>

#include <array>

#include "xaml_factory.hpp"

namespace kr::win {
// Krypton's mark: three round-capped gradient strokes over a white backplate,
// drawn on with TrimEnd. The visual lives on the compositor; no XAML layout
// pass runs while it draws.
class OnboardingMark {
 public:
  OnboardingMark();
  [[nodiscard]] const controls::Grid& Root() const noexcept { return root_; }
  // Draws the strokes on, staggered; at rest when `animate` is false.
  void Draw(bool animate);
  // The strokes undrawn, ready for the next Draw.
  void Reset() noexcept;
  // Pointer-driven, finite response. The normalized point is in [-1, 1].
  void Tilt(Point point, bool animate);

 private:
  void Build();
  controls::Grid root_;
  winrt::Microsoft::UI::Composition::ShapeVisual visual_{nullptr};
  // Backplate segments first, then the coloured strokes, per segment.
  std::array<winrt::Microsoft::UI::Composition::CompositionLineGeometry, 6>
      lines_{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  winrt::Microsoft::UI::Composition::ScalarKeyFrameAnimation draw_{nullptr};
  winrt::Microsoft::UI::Composition::SpringScalarNaturalMotionAnimation tilt_{
      nullptr};
};
}  // namespace kr::win
