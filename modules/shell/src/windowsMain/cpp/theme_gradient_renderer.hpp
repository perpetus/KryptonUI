#pragma once

#include <memory>

#include "krypton/ui/presentation/theme_gradient.hpp"
#include "xaml_factory.hpp"

namespace kr::win {
// Renders only when requested. GPU work runs on a sleeping graphics worker;
// the UI thread only updates the request and attaches the completed surface.
class ThemeGradientRenderer {
 public:
  ThemeGradientRenderer(const xaml::FrameworkElement& element,
                        const xaml::UIElement& fallback);
  ~ThemeGradientRenderer();
  ThemeGradientRenderer(const ThemeGradientRenderer&) = delete;
  ThemeGradientRenderer& operator=(const ThemeGradientRenderer&) = delete;

  [[nodiscard]] xaml::Media::Brush Brush() const;
  void Render(const ThemeGradient& gradient,
              Rect client,
              Rect region,
              float scale);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

void ShutdownThemeGradientWorker() noexcept;
}  // namespace kr::win
