#pragma once

#include <memory>

#include "krypton/ui/platform/glass.hpp"
#include "xaml_factory.hpp"

namespace kr::win {
// A retained XAML brush per visible native element. The worker changes only
// backing pixels; foreground elements and the Store are never touched by
// frames.
class GlassMaterials {
 public:
  GlassMaterials(
      ShellViewDelegate& delegate,
      std::move_only_function<Rect(const xaml::FrameworkElement&)> bounds);
  ~GlassMaterials();
  GlassMaterials(const GlassMaterials&) = delete;
  GlassMaterials& operator=(const GlassMaterials&) = delete;
  void Update(const ShellViewModel& vm);
  void Layout() noexcept;
  [[nodiscard]] xaml::Media::Brush For(const xaml::FrameworkElement& element,
                                       GlassRole role,
                                       const xaml::Media::Brush& fallback,
                                       float radius);
  void Place(const xaml::FrameworkElement& element, Rect bounds);
  [[nodiscard]] Result<void> Register(const GlassBackdropResource& resource);
  [[nodiscard]] Result<void> Submit(const GlassBackdropFrame& frame);
  [[nodiscard]] Result<void> Drop(std::uint64_t id);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
void ShutdownGlassWorker() noexcept;
}  // namespace kr::win
