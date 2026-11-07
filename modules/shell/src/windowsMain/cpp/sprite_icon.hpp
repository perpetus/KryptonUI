#pragma once

#include <windows.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <chrono>
#include <span>
#include <string>
#include <string_view>

#include "view_context.hpp"

namespace kr::win {

enum class Sprite {
  kLibrary,
  kMedia,
  kDownloads,
  kBoosts,
  kSpaces,
  kHistory,
  kRouting,
  kBoostIndicator
};

// Zen's animated library icons: a 36-frame strip seen through a one-frame
// window, like the CSS sprites it comes from. Frames step on the compositor.
// The strip is rasterized by SvgImageSource at the window's scale and only
// re-rendered when its colours or scale change.
class SpriteIcon {
 public:
  SpriteIcon(Sprite sprite, double size);
  ~SpriteIcon();
  SpriteIcon(const SpriteIcon&) = delete;
  SpriteIcon& operator=(const SpriteIcon&) = delete;

  // Zen's context-stroke and context-fill; a transparent fill draws nothing.
  void SetColors(Color stroke, Color fill, float scale, bool glow = false);
  void ShowFrame(int frame);
  // Steps from `from` to `to`, a frame at a time, like steps(36, jump-none).
  // The last frame stays shown when `hold` is set; otherwise frame 0 returns.
  bool Play(int from,
            int to,
            float milliseconds,
            bool hold,
            bool repeat = false) noexcept;
  bool Loop(bool running, float milliseconds) noexcept;
  // Hover reversals continue from the current frame, with no timer or polling.
  bool PlayTo(int frame, float full_duration_ms) noexcept;
  [[nodiscard]] const controls::Canvas& Root() const noexcept { return root_; }

 private:
  void Load();
  void LoadColored(const xaml::Media::Imaging::SvgImageSource& source,
                   std::span<const std::string_view> chunks,
                   int frames);
  void LoadSource(const xaml::Media::Imaging::SvgImageSource& source,
                  std::string_view svg,
                  int frames);
  void LoadGlow();
  [[nodiscard]] int CurrentFrame() const noexcept;
  std::span<const std::string_view> chunks_;
  double size_;
  int frames_;
  bool looping_ = false;
  controls::Canvas root_;
  controls::Canvas strip_;
  controls::Canvas glow_host_{nullptr};
  controls::Image image_;
  controls::Image box_image_{nullptr};
  controls::Image glow_image_{nullptr};
  xaml::Media::Imaging::SvgImageSource source_;
  xaml::Media::Imaging::SvgImageSource box_source_{nullptr};
  xaml::Media::Imaging::SvgImageSource glow_source_{nullptr};
  winrt::Microsoft::UI::Composition::SpriteVisual glow_visual_{nullptr};
  winrt::Microsoft::UI::Composition::DropShadow glow_shadow_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionVisualSurface glow_surface_{
      nullptr};
  winrt::Microsoft::UI::Composition::CompositionSurfaceBrush glow_brush_{
      nullptr};
  winrt::Microsoft::UI::Composition::Vector3KeyFrameAnimation animation_{
      nullptr};
  std::string svg_;
  Color stroke_, fill_;
  float scale_ = 0;
  float glow_scale_ = 0;
  bool glow_enabled_ = false;
  int frame_ = 0;
  int from_frame_ = 0, to_frame_ = 0;
  std::chrono::steady_clock::time_point started_;
  std::chrono::milliseconds duration_{0};
};
}  // namespace kr::win
