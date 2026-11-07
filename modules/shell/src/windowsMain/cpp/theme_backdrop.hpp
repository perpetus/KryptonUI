#pragma once

#include <array>
#include <optional>

#include "krypton/ui/presentation/theme_gradient.hpp"
#include "theme_gradient_renderer.hpp"
#include "theme_pattern.hpp"
#include "view_context.hpp"

namespace kr::win {
class ThemeBackdrop {
 public:
  ThemeBackdrop();
  void Render(const ThemeVm& theme,
              Rect client,
              Rect region,
              bool high_contrast);
  void Render(const ThemeGradient& gradient,
              float texture,
              Rect client,
              Rect region,
              bool high_contrast);
  [[nodiscard]] const controls::Grid& Root() const noexcept { return root_; }

 private:
  struct Layer {
    controls::Border element{nullptr};
    media::LinearGradientBrush linear{nullptr};
    media::RadialGradientBrush radial{nullptr};
    std::array<media::GradientStop, 4> line_stops{nullptr, nullptr, nullptr,
                                                  nullptr};
    std::array<media::GradientStop, 4> radial_stops{nullptr, nullptr, nullptr,
                                                    nullptr};
  };
  void EnsureLayer(std::size_t index, bool radial);
  void Linear(std::size_t index, const ThemeGradientLayer& layer);
  void Radial(std::size_t index, const ThemeGradientLayer& layer);
  controls::Grid root_;
  controls::Grid gradient_layers_;
  controls::Border smooth_;
  std::unique_ptr<ThemeGradientRenderer> renderer_;
  std::array<Layer, 3> layers_;
  media::SolidColorBrush base_;
  controls::Image grain_image_{nullptr};
  ThemePattern grain_;
  std::optional<ThemeGradient> last_;
  Rect client_, region_;
  float texture_ = 0;
  float scale_ = 1;
  bool smoothing_unavailable_ = false;
};
}  // namespace kr::win
