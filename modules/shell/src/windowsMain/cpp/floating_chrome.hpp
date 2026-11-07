#pragma once

#include <winrt/Microsoft.UI.Composition.h>

#include "view_context.hpp"

namespace kr::win {

// A compact reveal's retained native surface: clipped rounded content over a
// compositor shadow. Shared layout supplies both the surface and island bounds.
class FloatingChrome {
 public:
  void Initialize(const controls::Grid& surface);
  void Update(Rect surface,
              Rect island,
              bool floating,
              bool dark,
              bool high_contrast);
  [[nodiscard]] const controls::Grid& Root() const noexcept { return root_; }

 private:
  void EnsureShadow();
  controls::Grid root_;
  controls::Grid shadow_host_;
  controls::Grid surface_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionRoundedRectangleGeometry clip_{
      nullptr};
  winrt::Microsoft::UI::Composition::CompositionGeometricClip surface_clip_{
      nullptr};
  winrt::Microsoft::UI::Composition::LayerVisual layer_{nullptr};
  winrt::Microsoft::UI::Composition::ShapeVisual shape_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionRoundedRectangleGeometry
      outline_{nullptr};
  winrt::Microsoft::UI::Composition::DropShadow shadow_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionColorBrush fill_{nullptr};
};

}  // namespace kr::win
