#pragma once

#include <windows.h>

#include <array>
#include <memory>
#include <span>

#include "krypton/fixtures/mock_browser.hpp"
#include "page_surface.hpp"

namespace kr::preview {
// An owning snapshot of the host's layers, excluding every KryptonUI island.
// Both the window painter and backdrop producer use this exact scene.
struct HostScene {
  RECT client{};
  KrRect content{};
  float radius = 0, scale = 1, zoom = 1;
  bool dark = false, has_browser = false, checkerboard = false;
  // Quality glass: the frame stays unpainted over the DWM system backdrop.
  bool transparent = false;
  unsigned phase = 0;
  std::array<std::uint32_t, 2> gradient{};
  KrWorkspaceTheme theme{};
  std::uint64_t generation = 0;
  fixtures::MockBrowser::PageLayout layout;
};

void PaintHostScene(HDC dc,
                    const HostScene& scene,
                    PageSurface& page_surface,
                    std::span<const HFONT, 3> fonts);

// Makes the rounded page opaque in a top-down 32-bit buffer whose frame was
// left unpainted; everything else keeps the alpha the buffer was cleared to.
void OpaquePage(std::span<RGBQUAD> pixels, int stride, const HostScene& scene);

// GDI is the mock browser's page renderer. Its worker renders only host layers
// into a retained DIB, then uploads BGRA to the shared GPU texture. No window
// capture, UI-thread rasterization or readback is involved.
class HostBackdropPainter {
 public:
  [[nodiscard]] std::span<const std::uint32_t> Paint(const HostScene& scene,
                                                     KrRect bounds,
                                                     UINT width,
                                                     UINT height);

 private:
  struct DcDeleter {
    void operator()(HDC dc) const noexcept {
      if (dc)
        DeleteDC(dc);
    }
  };
  std::unique_ptr<std::remove_pointer_t<HDC>, DcDeleter> dc_;
  std::unique_ptr<std::remove_pointer_t<HBITMAP>, GdiDeleter> bitmap_;
  std::array<std::unique_ptr<std::remove_pointer_t<HFONT>, GdiDeleter>, 3>
      fonts_;
  std::uint32_t* pixels_ = nullptr;  // borrowed from bitmap_
  UINT width_ = 0, height_ = 0;
  float font_scale_ = 0;
  PageSurface page_surface_;
};
}  // namespace kr::preview
