#include "theme_pattern.hpp"

#include <robuffer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "krypton/ui/design/grain.generated.hpp"
#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/presentation/boost.hpp"

namespace kr::win {
namespace {
BYTE* Pixels(
    const winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap& bitmap) {
  BYTE* bytes = nullptr;
  winrt::check_hresult(bitmap.PixelBuffer()
                           .as<::Windows::Storage::Streams::IBufferByteAccess>()
                           ->Buffer(&bytes));
  return bytes;
}
}  // namespace

bool ThemePattern::Resize(int width, int height) {
  width = std::max(1, width);
  height = std::max(1, height);
  if (bitmap_ && width_ == width && height_ == height)
    return false;
  width_ = width;
  height_ = height;
  bitmap_ = winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap(width,
                                                                        height);
  return true;
}
auto ThemePattern::HueWheel(int width, int height)
    -> winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap {
  if (!Resize(width, height))
    return bitmap_;
  auto* pixels = Pixels(bitmap_);
  for (int y = 0; y < height_; ++y)
    for (int x = 0; x < width_; ++x) {
      const auto color =
          BoostSpectrumColor(
              {static_cast<float>(x) / static_cast<float>(width_),
               static_cast<float>(y) / static_cast<float>(height_)})
              .argb;
      std::memcpy(pixels + (static_cast<std::size_t>(y) * width_ + x) * 4,
                  &color, sizeof(color));
    }
  bitmap_.Invalidate();
  return bitmap_;
}

auto ThemePattern::Dots(int width, int height, Color ink)
    -> winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap {
  const bool resized = Resize(width, height);
  if (!resized && ink_ == ink)
    return bitmap_;
  ink_ = ink;
  const auto& token = design::kTokens.theme_editor;
  const int pitch =
      std::max(1, static_cast<int>(std::lround(token.board_dot_pitch)));
  const int phase = static_cast<int>(std::lround(token.board_dot_phase));
  std::vector<std::uint32_t> tile(static_cast<std::size_t>(pitch) * pitch);
  const auto premul = [&](std::uint8_t channel) {
    return static_cast<std::uint32_t>(channel) * ink.a() / 255;
  };
  const auto argb = static_cast<std::uint32_t>(ink.a()) << 24 |
                    premul(ink.r()) << 16 | premul(ink.g()) << 8 |
                    premul(ink.b());
  for (int y = 0; y < pitch; ++y)
    for (int x = 0; x < pitch; ++x)
      if (std::hypot(static_cast<float>(x - phase),
                     static_cast<float>(y - phase)) <= token.board_dot_radius)
        tile[static_cast<std::size_t>(y) * pitch + x] = argb;
  auto* pixels = Pixels(bitmap_);
  for (int y = 0; y < height_; ++y)
    for (int x = 0; x < width_; x += pitch)
      std::memcpy(pixels + (static_cast<std::size_t>(y) * width_ + x) * 4,
                  tile.data() + static_cast<std::size_t>(y % pitch) * pitch,
                  static_cast<std::size_t>(std::min(pitch, width_ - x)) * 4);
  bitmap_.Invalidate();
  return bitmap_;
}

auto ThemePattern::Grain(int width, int height, Point origin)
    -> winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap {
  const bool resized = Resize(width, height);
  if (!resized && origin_ == origin)
    return bitmap_;
  origin_ = origin;
  const int ox = static_cast<int>(std::lround(origin.x));
  const int oy = static_cast<int>(std::lround(origin.y));
  const auto wrap = [](int value, int range) {
    return (value % range + range) % range;
  };
  auto* pixels = Pixels(bitmap_);
  for (int y = 0; y < height_; ++y) {
    int x = 0;
    while (x < width_) {
      const int sx = wrap(x + ox, design::kGrainWidth);
      const int count = std::min(design::kGrainWidth - sx, width_ - x);
      const auto* row =
          design::kGrainPixels.data() +
          static_cast<std::size_t>(wrap(y + oy, design::kGrainHeight)) *
              design::kGrainWidth +
          sx;
      std::memcpy(pixels + (static_cast<std::size_t>(y) * width_ + x) * 4, row,
                  static_cast<std::size_t>(count) * 4);
      x += count;
    }
  }
  bitmap_.Invalidate();
  return bitmap_;
}
}  // namespace kr::win
