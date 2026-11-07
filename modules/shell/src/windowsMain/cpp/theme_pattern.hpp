#pragma once

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include "krypton/ui/core/color.hpp"
#include "krypton/ui/core/geometry.hpp"

namespace kr::win {
// Raster patterns are cached by geometry and theme. Opacity is applied to the
// native image, so dragging either editor control never regenerates pixels.
class ThemePattern {
 public:
  winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap Dots(int width,
                                                                   int height,
                                                                   Color ink);
  winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap
  Grain(int width, int height, Point origin);
  winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap HueWheel(
      int width,
      int height);

 private:
  bool Resize(int width, int height);
  winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap bitmap_{nullptr};
  int width_ = 0, height_ = 0;
  Point origin_{-1, -1};
  Color ink_;
};
}  // namespace kr::win
