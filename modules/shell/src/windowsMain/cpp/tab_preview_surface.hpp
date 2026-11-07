#pragma once

#include <windows.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.Graphics.h>

#include <chrono>
#include <cmath>
#include <memory>

#include "native_input.hpp"
#include "native_motion.hpp"
#include "xaml_factory.hpp"

namespace kr::win {

// One shared, lazy transient island. Only the card's rectangle covers the
// host page; the steady sidebar island never owns the visible preview.
class TabPreviewSurface {
 public:
  TabPreviewSurface(HWND parent, NativeMotion& motion)
      : parent_(parent), motion_(motion) {}
  ~TabPreviewSurface();
  TabPreviewSurface(const TabPreviewSurface&) = delete;
  TabPreviewSurface& operator=(const TabPreviewSurface&) = delete;

  void Render(std::uint32_t request,
              const controls::Border& card,
              const ShellViewModel& vm,
              Rect anchor);
  void Close(std::uint32_t request) noexcept;
  void Reset() noexcept;
  [[nodiscard]] bool IsOpen() const noexcept { return request_ != 0; }
  [[nodiscard]] bool ContainsAnchor(Point point) const noexcept {
    return anchor_.Contains(point);
  }

  // Hover timing shared by every tab's card: the delay before a card opens,
  // which is none for a moment after the pointer leaves an open card.
  void SetShowDelay(std::chrono::milliseconds delay) noexcept {
    show_delay_ = delay;
  }
  [[nodiscard]] std::chrono::milliseconds ShowDelay() const noexcept;
  void NoteExit() noexcept { exited_ = std::chrono::steady_clock::now(); }

 private:
  void EnsureSource();
  void Reposition() noexcept;
  HWND parent_;
  NativeInputTarget input_target_;
  NativeMotion& motion_;
  ContentFade fade_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  xaml::Hosting::DesktopWindowXamlSource source_{nullptr};
  controls::Grid root_{nullptr};
  controls::Border card_{nullptr};
  std::uint32_t request_ = 0;
  std::chrono::milliseconds show_delay_{
      std::lround(kTokens.tab_hover.show_delay_minimum_ms)};
  std::chrono::steady_clock::time_point exited_;
  Rect client_, sidebar_, anchor_, chrome_;
  bool horizontal_ = false, right_to_left_ = false, high_contrast_ = false;
  float scale_ = 1;
  bool on_right_ = false, visible_ = false, positioning_ = false;
  winrt::Windows::Graphics::RectInt32 bounds_{};
  xaml::FrameworkElement::Loaded_revoker loaded_;
  xaml::FrameworkElement::SizeChanged_revoker sized_;
};

}  // namespace kr::win
