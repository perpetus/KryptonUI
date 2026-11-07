#pragma once

#include "xaml_factory.hpp"

namespace kr::win {

// The stock Slider template consumes these theme resources as x:Double,
// including the pre/post values used by Grid row definitions on first reveal.
inline void SetMediaSliderResources(const controls::Slider& slider,
                                    const xaml::Media::Brush& track) {
  const auto resources = slider.Resources();
  const auto metric = [&](std::wstring_view key, double value) {
    resources.Insert(winrt::box_value(winrt::hstring(key)),
                     winrt::box_value(value));
  };
  constexpr double height = kTokens.media_controls.progress_height;
  metric(L"SliderTrackThemeHeight", height);
  metric(L"SliderHorizontalThumbWidth", 2 * height);
  metric(L"SliderHorizontalThumbHeight", 2 * height);
  metric(L"SliderInnerThumbWidth", 2 * height);
  metric(L"SliderInnerThumbHeight", 2 * height);
  metric(L"SliderPreContentMargin", height);
  metric(L"SliderPostContentMargin", height);
  metric(L"SliderHorizontalHeight", 4 * height);
  for (const auto key : {L"SliderTrackFill", L"SliderTrackFillPointerOver",
                         L"SliderTrackFillPressed"}) {
    resources.Insert(winrt::box_value(winrt::hstring(key)), track);
  }
}

}  // namespace kr::win
