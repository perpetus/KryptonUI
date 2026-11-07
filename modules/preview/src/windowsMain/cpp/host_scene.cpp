#include "host_scene.hpp"

#include <winrt/base.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "krypton/ui/design/tokens.hpp"

namespace kr::preview {
namespace {
using kr::design::kTokens;
COLORREF Color(std::uint32_t argb) noexcept {
  return RGB((argb >> 16) & 255, (argb >> 8) & 255, argb & 255);
}
std::wstring Wide(std::string_view value) {
  return std::wstring(winrt::to_hstring(value));
}
}  // namespace
void PaintHostScene(HDC dc,
                    const HostScene& scene,
                    PageSurface& page_surface,
                    std::span<const HFONT, 3> fonts) {
  const bool dark = scene.dark;
  const auto& palette = dark ? kTokens.dark : kTokens.light;
  const auto client = scene.client;
  RECT card{};
  int diameter = 0;
  std::array<std::uint32_t, 2> gradient{palette.gradient_base,
                                        palette.gradient_base};
  if (scene.has_browser) {
    const auto content = scene.content;
    const float scale = scene.scale;
    card = {
        static_cast<LONG>(std::lround(content.x * scale)),
        static_cast<LONG>(std::lround(content.y * scale)),
        static_cast<LONG>(std::lround((content.x + content.width) * scale)),
        static_cast<LONG>(std::lround((content.y + content.height) * scale))};
    diameter = static_cast<int>(std::lround(scene.radius * scale * 2));
    const auto reported = scene.gradient;
    if ((reported[0] >> 24) != 0 && (reported[1] >> 24) != 0)
      gradient = reported;
  }
  const BackdropState backdrop{
      .gradient = gradient,
      .theme = scene.has_browser ? &scene.theme : nullptr,
      .generation = scene.generation,
      .scale = scene.scale,
      .dark = dark,
      .transparent = scene.transparent};
  page_surface.Paint(dc, client, card, diameter, backdrop,
                     palette.page_background);
  if (scene.checkerboard) {
    const int saved = SaveDC(dc);
    SelectClipRgn(dc, page_surface.ClipRegion());
    const auto& t = kTokens.preview;
    const int tile = std::max(
        1, static_cast<int>(std::lround(t.glass_grid_tile * scene.scale)));
    const int gap =
        static_cast<int>(std::lround(t.glass_grid_gap * scene.scale));
    const int shift =
        static_cast<int>(std::lround(scene.phase * scene.scale)) % tile;
    const std::array colors{t.glass_grid_first, t.glass_grid_second,
                            t.glass_grid_third, t.glass_grid_fourth};
    const std::unique_ptr<std::remove_pointer_t<HBRUSH>, GdiDeleter> base(
        CreateSolidBrush(Color(t.glass_grid_background)));
    FillRect(dc, &client, base.get());
    for (int y = 0; y < client.bottom; y += tile) {
      for (int x = -tile; x < client.right; x += tile) {
        const auto index =
            static_cast<std::size_t>((x / tile + y / tile + 4) % 4);
        const std::unique_ptr<std::remove_pointer_t<HBRUSH>, GdiDeleter> brush(
            CreateSolidBrush(Color(colors[index])));
        const RECT rect{x + shift, y, x + shift + tile - gap, y + tile - gap};
        FillRect(dc, &rect, brush.get());
      }
    }
    RestoreDC(dc, saved);
  }
  if (scene.has_browser) {
    const float scale = scene.scale;
    const auto pixels = [scale](KrRect bounds) -> RECT {
      return {
          static_cast<LONG>(std::lround(bounds.x * scale)),
          static_cast<LONG>(std::lround(bounds.y * scale)),
          static_cast<LONG>(std::lround((bounds.x + bounds.width) * scale)),
          static_cast<LONG>(std::lround((bounds.y + bounds.height) * scale))};
    };
    const auto& layout = scene.layout;
    if (layout.divider_count) {
      const HBRUSH divider = CreateSolidBrush(Color(palette.gradient_base));
      for (std::size_t i = 0; i < layout.divider_count; ++i) {
        const RECT bounds = pixels(layout.dividers[i].bounds);
        FillRect(dc, &bounds, divider);
      }
      DeleteObject(divider);
    }
    for (std::size_t i = 0; i < layout.count; ++i) {
      const auto& surface = layout.surfaces[i];
      const auto& page = surface.page;
      const RECT pane_card = pixels(surface.bounds);
      if (surface.glance) {
        const RECT frame = pixels(layout.glance.frame);
        const HBRUSH background =
            CreateSolidBrush(Color(palette.page_background));
        FillRect(dc, &frame, background);
        DeleteObject(background);
        const HBRUSH border = CreateSolidBrush(Color(palette.text_muted));
        FrameRect(dc, &frame, border);
        DeleteObject(border);
      }
      if (!page.blank) {
        const int saved = SaveDC(dc);
        SelectClipRgn(dc, page_surface.ClipRegion());
        IntersectClipRect(dc, pane_card.left, pane_card.top, pane_card.right,
                          pane_card.bottom);
        const float zoom = scene.zoom;
        if (zoom != 1) {
          SetGraphicsMode(dc, GM_ADVANCED);
          const XFORM transform{zoom,
                                0,
                                0,
                                zoom,
                                static_cast<float>(pane_card.left) * (1 - zoom),
                                static_cast<float>(pane_card.top) * (1 - zoom)};
          SetWorldTransform(dc, &transform);
        }
        SetBkMode(dc, TRANSPARENT);
        const LONG inset =
            static_cast<LONG>(kTokens.preview.page_inset * scale);
        RECT text{pane_card.left + inset, pane_card.top + inset,
                  pane_card.right - inset, pane_card.bottom - inset};
        auto line = [&](std::wstring_view value, HFONT font,
                        std::uint32_t color, float height) {
          SelectObject(dc, font);
          SetTextColor(dc, Color(color));
          RECT row = text;
          row.bottom = row.top + static_cast<LONG>(height * scale);
          DrawTextW(
              dc, value.data(), static_cast<int>(value.size()), &row,
              DT_LEFT | DT_TOP | DT_END_ELLIPSIS | DT_NOPREFIX | DT_SINGLELINE);
          text.top = row.bottom;
        };
        line(Wide(page.title), fonts[1], palette.text,
             kTokens.preview.page_title_size + kTokens.spacing.section_gap);
        line(Wide(page.url), fonts[0], palette.text_muted,
             kTokens.preview.page_line_height + kTokens.spacing.section_gap);
        line(L"Your workspace, your way.", fonts[2], palette.text,
             kTokens.preview.page_line_height + kTokens.spacing.section_gap);
        line(L"Open a tab, switch workspaces, or make this space your own.",
             fonts[0], palette.text_muted, kTokens.preview.page_line_height);
        line(
            L"Right-click the sidebar for layouts, compact mode, and theme "
            L"colors.",
            fonts[0], palette.text_muted, kTokens.preview.page_line_height);
        text.top += static_cast<LONG>(kTokens.spacing.section_gap * scale);
        line(L"Ctrl+L  Address     Ctrl+T  New tab     Ctrl+W  Close tab",
             fonts[0], palette.text_muted, kTokens.preview.page_line_height);
        line(L"Ctrl+Shift+R  Restart preview", fonts[0], palette.text_muted,
             kTokens.preview.page_line_height);
        line(
            L"Ctrl+Alt+C  Compact mode     Ctrl+Alt+S  Collapse sidebar     "
            L"F11  "
            L"Full screen",
            fonts[0], palette.text_muted, kTokens.preview.page_line_height);
        RestoreDC(dc, saved);
      }
    }
    if (layout.count && layout.surfaces[layout.count - 1].glance) {
      const RECT toolbar = pixels(layout.glance.toolbar);
      const auto width = (toolbar.right - toolbar.left) / 3;
      constexpr std::array labels{L"Close", L"Expand", L"Split"};
      const int saved = SaveDC(dc);
      SelectClipRgn(dc, page_surface.ClipRegion());
      SelectObject(dc, fonts[0]);
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, Color(palette.text));
      for (std::size_t i = 0; i < labels.size(); ++i) {
        RECT button = toolbar;
        button.left += static_cast<LONG>(i) * width;
        button.right =
            i + 1 == labels.size() ? toolbar.right : button.left + width;
        DrawTextW(dc, labels[i], -1, &button,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      }
      RestoreDC(dc, saved);
    }
  }
}
void OpaquePage(std::span<RGBQUAD> pixels, int stride, const HostScene& scene) {
  if (!scene.has_browser || stride <= 0)
    return;
  const auto rows = static_cast<LONG>(pixels.size() / stride);
  const float scale = scene.scale;
  const LONG left = std::max(0L, std::lround(scene.content.x * scale)),
             top = std::max(0L, std::lround(scene.content.y * scale));
  const LONG right =
      std::min(static_cast<LONG>(stride),
               std::lround((scene.content.x + scene.content.width) * scale));
  const LONG bottom = std::min(
      rows, std::lround((scene.content.y + scene.content.height) * scale));
  const float radius = std::min(
      {scene.radius * scale, (right - left) / 2.0f, (bottom - top) / 2.0f});
  for (LONG y = top; y < bottom; ++y) {
    // Horizontal inset of the rounded page at this row's center.
    const float from_edge = std::min(y + 0.5f - top, bottom - (y + 0.5f));
    float inset = 0;
    if (radius > 0 && from_edge < radius) {
      const float d = radius - from_edge;
      inset = radius - std::sqrt(std::max(0.0f, radius * radius - d * d));
    }
    const LONG from = left + static_cast<LONG>(std::lround(inset)),
               to = right - static_cast<LONG>(std::lround(inset));
    auto* row = pixels.data() + static_cast<std::size_t>(y) * stride;
    for (LONG x = from; x < to; ++x)
      row[x].rgbReserved = 255;
  }
}
std::span<const std::uint32_t> HostBackdropPainter::Paint(
    const HostScene& scene,
    KrRect bounds,
    UINT width,
    UINT height) {
  if (!dc_)
    dc_.reset(CreateCompatibleDC(nullptr));
  winrt::check_bool(dc_.get() != nullptr);
  if (!bitmap_ || width_ != width || height_ != height) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    bitmap_.reset(CreateDIBSection(dc_.get(), &info, DIB_RGB_COLORS, &pixels,
                                   nullptr, 0));
    winrt::check_bool(bitmap_.get() != nullptr);
    pixels_ = static_cast<std::uint32_t*>(pixels);
    width_ = width;
    height_ = height;
  }
  if (font_scale_ != scene.scale) {
    const std::wstring family(winrt::to_hstring(kTokens.type.family));
    const std::array sizes{kTokens.type.size, kTokens.preview.page_title_size,
                           kTokens.preview.page_heading_size};
    for (std::size_t i = 0; i < fonts_.size(); ++i) {
      fonts_[i].reset(CreateFontW(
          -static_cast<int>(std::lround(sizes[i] * scene.scale)), 0, 0, 0,
          i == 0 ? FW_NORMAL : FW_SEMIBOLD, FALSE, FALSE, FALSE,
          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
          CLEARTYPE_QUALITY, DEFAULT_PITCH, family.c_str()));
      winrt::check_bool(fonts_[i].get() != nullptr);
    }
    font_scale_ = scene.scale;
  }
  struct Restore {
    HDC dc;
    int saved;
    ~Restore() { RestoreDC(dc, saved); }
  } restore{dc_.get(), SaveDC(dc_.get())};
  const auto count = static_cast<std::size_t>(width) * height;
  std::fill_n(
      pixels_, count,
      scene.dark ? kTokens.dark.gradient_base : kTokens.light.gradient_base);
  SelectObject(dc_.get(), bitmap_.get());
  SetViewportOrgEx(
      dc_.get(), -static_cast<int>(std::lround(bounds.x * scene.scale)),
      -static_cast<int>(std::lround(bounds.y * scene.scale)), nullptr);
  const std::array fonts{fonts_[0].get(), fonts_[1].get(), fonts_[2].get()};
  PaintHostScene(dc_.get(), scene, page_surface_, fonts);
  GdiFlush();
  // GDI leaves the unused DIB alpha byte undefined. The composed host is
  // opaque.
  for (std::size_t i = 0; i < count; ++i)
    pixels_[i] |= 0xff000000u;
  return {pixels_, count};
}
}  // namespace kr::preview
