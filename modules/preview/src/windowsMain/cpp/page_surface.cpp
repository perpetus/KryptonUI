#include "page_surface.hpp"

#include <cmath>

namespace kr::preview {
namespace {

void PaintBackdrop(HDC dc,
                   const RECT& client,
                   const RECT& page,
                   int radius,
                   const std::array<std::uint32_t, 2>& colors) {
  const double width = static_cast<double>(client.right - client.left);
  const double height = static_cast<double>(client.bottom - client.top);
  const double length = width * width + height * height;
  if (length == 0)
    return;
  const auto vertex = [&](LONG x, LONG y, double at) {
    const auto channel = [&](unsigned shift) {
      const double first = (colors[0] >> shift) & 255;
      const double last = (colors[1] >> shift) & 255;
      return static_cast<COLOR16>(
          std::lround((first + (last - first) * at) * 256));
    };
    return TRIVERTEX{x, y, channel(16), channel(8), channel(0), 0};
  };
  std::array vertices{
      vertex(client.left, client.top, 0),
      vertex(client.right, client.top, width * width / length),
      vertex(client.right, client.bottom, 1),
      vertex(client.left, client.bottom, height * height / length)};
  std::array<GRADIENT_TRIANGLE, 2> triangles{{{0, 1, 2}, {0, 2, 3}}};
  const int saved = SaveDC(dc);
  // The opaque page covers its interior; only paint the surrounding chrome
  // and the cutouts at its corners.
  if (page.right > page.left && page.bottom > page.top)
    ExcludeClipRect(dc, page.left + radius, page.top + radius,
                    page.right - radius, page.bottom - radius);
  GradientFill(dc, vertices.data(), static_cast<ULONG>(vertices.size()),
               triangles.data(), static_cast<ULONG>(triangles.size()),
               GRADIENT_FILL_TRIANGLE);
  RestoreDC(dc, saved);
}

}  // namespace

void PageSurface::Paint(HDC dc,
                        const RECT& client,
                        const RECT& page,
                        int diameter,
                        const BackdropState& backdrop,
                        std::uint32_t page_color) {
  if (!backdrop.transparent &&
      !backdrop_.Paint(dc, client, page, (diameter + 1) / 2, backdrop))
    PaintBackdrop(dc, client, page, (diameter + 1) / 2, backdrop.gradient);
  POINT origin{};
  GetViewportOrgEx(dc, &origin);
  if (!region_ || !EqualRect(&bounds_, &page) || diameter_ != diameter ||
      origin_.x != origin.x || origin_.y != origin.y) {
    // SelectClipRgn consumes device coordinates, whereas FillRect/text use
    // logical coordinates. Backdrop regions paint with a shifted viewport.
    region_.reset(CreateRoundRectRgn(
        page.left + origin.x, page.top + origin.y, page.right + origin.x,
        page.bottom + origin.y, diameter, diameter));
    bounds_ = page;
    origin_ = origin;
    diameter_ = diameter;
  }
  if (!brush_ || color_ != page_color) {
    brush_.reset(CreateSolidBrush(RGB(
        (page_color >> 16) & 255, (page_color >> 8) & 255, page_color & 255)));
    color_ = page_color;
  }
  const int saved = SaveDC(dc);
  SelectClipRgn(dc, region_.get());
  FillRect(dc, &page, brush_.get());
  RestoreDC(dc, saved);
}

}  // namespace kr::preview
