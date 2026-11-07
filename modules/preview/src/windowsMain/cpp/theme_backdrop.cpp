// SPDX-License-Identifier: MPL-2.0
// Host rendering of the gradient layers described by ZenGradientGenerator.mjs.
#include "theme_backdrop.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "krypton/ui/design/grain.generated.hpp"
#include "krypton/ui/design/tokens.generated.hpp"
#include "krypton/ui/presentation/theme_gradient.hpp"

namespace kr::preview {
bool ThemeBackdrop::Paint(HDC dc,
                          const RECT& client,
                          const RECT& page,
                          int radius,
                          const BackdropState& state) {
  if (!state.theme || !(state.theme->flags & KR_THEME_CUSTOMIZED) ||
      state.theme->point_count > 3)
    return false;
  const LONG width = client.right - client.left,
             height = client.bottom - client.top;
  if (width <= 0 || height <= 0)
    return true;
  if (!initialized_ || generation_ != state.generation || dark_ != state.dark ||
      !EqualRect(&bounds_, &client)) {
    initialized_ = true;
    generation_ = state.generation;
    bounds_ = client;
    dark_ = state.dark;
    std::array<Color, 3> colors;
    for (std::size_t i = 0; i < state.theme->point_count; ++i)
      colors[i] = Color{state.theme->points[i].color};
    const auto gradient =
        MakeThemeGradient(Color{design::PaletteFor(state.dark).gradient_base},
                          std::span{colors}.first(state.theme->point_count),
                          state.theme->opacity);
    const Rect area{static_cast<float>(client.left),
                    static_cast<float>(client.top), static_cast<float>(width),
                    static_cast<float>(height)};
    const auto steps = static_cast<std::size_t>(std::clamp(
        design::kTokens.theme_editor.mesh_subdivisions, 1.0f, 64.0f));
    const auto stride = steps + 1;
    vertices_.resize(stride * stride);
    triangles_.resize(steps * steps * 2);
    for (std::size_t y = 0; y <= steps; ++y) {
      for (std::size_t x = 0; x <= steps; ++x) {
        const float u = static_cast<float>(x) / static_cast<float>(steps),
                    v = static_cast<float>(y) / static_cast<float>(steps);
        const auto rgb = SampleThemeGradient(gradient, area,
                                             {area.x + u * area.width,
                                              area.y + v * area.height})
                             .argb;
        vertices_[y * stride + x] = {
            client.left +
                static_cast<LONG>(std::lround(u * static_cast<float>(width))),
            client.top +
                static_cast<LONG>(std::lround(v * static_cast<float>(height))),
            static_cast<COLOR16>(((rgb >> 16) & 255) * 256),
            static_cast<COLOR16>(((rgb >> 8) & 255) * 256),
            static_cast<COLOR16>((rgb & 255) * 256),
            0};
        if (x < steps && y < steps) {
          const auto a = static_cast<ULONG>(y * stride + x), b = a + 1;
          const auto c = a + static_cast<ULONG>(stride), d = c + 1;
          const auto index = (y * steps + x) * 2;
          triangles_[index] = {a, b, d};
          triangles_[index + 1] = {a, d, c};
        }
      }
    }
  }
  const int saved = SaveDC(dc);
  if (page.right > page.left && page.bottom > page.top)
    ExcludeClipRect(dc, page.left + radius, page.top + radius,
                    page.right - radius, page.bottom - radius);
  GradientFill(dc, vertices_.data(), static_cast<ULONG>(vertices_.size()),
               triangles_.data(), static_cast<ULONG>(triangles_.size()),
               GRADIENT_FILL_TRIANGLE);
  if (state.theme->texture > 0) {
    if (!grain_) {
      BITMAPINFO info{};
      info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth = design::kGrainWidth;
      info.bmiHeader.biHeight = -design::kGrainHeight;
      info.bmiHeader.biPlanes = 1;
      info.bmiHeader.biBitCount = 32;
      info.bmiHeader.biCompression = BI_RGB;
      void* pixels = nullptr;
      grain_.reset(
          CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0));
      if (grain_)
        std::memcpy(pixels, design::kGrainPixels.data(),
                    design::kGrainPixels.size() * sizeof(std::uint32_t));
    }
    if (grain_) {
      if (!grain_dc_)
        grain_dc_.reset(CreateCompatibleDC(dc));
      if (grain_dc_) {
        const auto memory = grain_dc_.get();
        SelectObject(memory, grain_.get());
        const int tile_w = std::max(
            1,
            static_cast<int>(std::lround(design::kGrainWidth * state.scale)));
        const int tile_h = std::max(
            1,
            static_cast<int>(std::lround(design::kGrainHeight * state.scale)));
        const BLENDFUNCTION blend{
            AC_SRC_OVER, 0,
            static_cast<BYTE>(std::lround(state.theme->texture * 255)),
            AC_SRC_ALPHA};
        for (LONG y = client.top; y < client.bottom; y += tile_h)
          for (LONG x = client.left; x < client.right; x += tile_w)
            AlphaBlend(dc, x, y, tile_w, tile_h, memory, 0, 0,
                       design::kGrainWidth, design::kGrainHeight, blend);
      }
    }
  }
  RestoreDC(dc, saved);
  return true;
}
}  // namespace kr::preview
