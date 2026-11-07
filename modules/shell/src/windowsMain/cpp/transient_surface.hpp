#pragma once

#include <windows.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.Graphics.h>

#include "island_hide.hpp"
#include "native_input.hpp"
#include "xaml_factory.hpp"

namespace kr::win {
// One stable, client-sized XamlRoot for an explicit transient surface. Raising
// the child site affects only siblings in this browser window.
class TransientSurface {
 public:
  explicit TransientSurface(HWND parent) : parent_(parent) {}
  ~TransientSurface();
  TransientSurface(const TransientSurface&) = delete;
  TransientSurface& operator=(const TransientSurface&) = delete;
  TransientSurface(TransientSurface&&) = delete;
  TransientSurface& operator=(TransientSurface&&) = delete;

  void Ensure();
  void Show(Rect client, float scale, xaml::ElementTheme theme);
  void Hide() noexcept;
  void Raise() noexcept;
  [[nodiscard]] const controls::Grid& Root() const noexcept { return root_; }
  [[nodiscard]] Rect ClientBounds() const;
  [[nodiscard]] bool IsReady() const;
  // The island's child window, for frame hit testing; null before Ensure.
  [[nodiscard]] HWND Window() const noexcept;

 private:
  HWND parent_;
  // Where keyboard focus was when the surface appeared; it returns there.
  HWND return_focus_ = nullptr;
  NativeInputTarget input_target_;
  IslandHide hide_;
  xaml::Hosting::DesktopWindowXamlSource source_{nullptr};
  controls::Grid root_{nullptr};
  Rect client_;
  winrt::Windows::Graphics::RectInt32 bounds_{};
  bool visible_ = false;
};
}  // namespace kr::win
