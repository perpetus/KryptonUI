// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once

#include "native_motion.hpp"
#include "native_theme.hpp"

namespace kr::win {
// ZenFolder.mjs's layered SVG, drawn once per retained native row. Only a
// compositor scalar changes during the open/close transition.
class FolderIcon {
 public:
  FolderIcon();
  ~FolderIcon();
  FolderIcon(const FolderIcon&) = delete;
  FolderIcon& operator=(const FolderIcon&) = delete;
  void Update(bool open,
              bool active,
              std::string_view glyph,
              const ThemeVm& theme,
              Brushes& brushes,
              const NativeMotion& motion,
              bool animate);
  void Reset() noexcept;
  [[nodiscard]] const controls::Viewbox& Root() const noexcept { return root_; }

 private:
  controls::Viewbox root_{nullptr};
  controls::Canvas canvas_{nullptr}, back_{nullptr}, front_{nullptr};
  controls::TextBlock emblem_{nullptr};
  xaml::UIElement dots_{nullptr};
  winrt::Microsoft::UI::Composition::Visual back_visual_{nullptr},
      front_visual_{nullptr}, emblem_visual_{nullptr}, dots_visual_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionPropertySet progress_{nullptr};
  winrt::Microsoft::UI::Composition::ScalarKeyFrameAnimation animation_{
      nullptr};
  winrt::Microsoft::UI::Composition::ScalarKeyFrameAnimation opacity_{nullptr};
  winrt::Microsoft::UI::Composition::CubicBezierEasingFunction easing_{nullptr};
  bool open_ = false;
  bool dots_visible_ = false;
  bool initialized_ = false;
  bool theme_ready_ = false, dark_ = false, contrast_ = false;
  Color accent_;
  std::string glyph_;
};
}  // namespace kr::win
