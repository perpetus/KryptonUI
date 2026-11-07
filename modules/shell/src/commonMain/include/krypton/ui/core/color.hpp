#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace kr {

// 0xAARRGGBB, straight (not premultiplied) alpha.
struct Color {
  std::uint32_t argb = 0;

  [[nodiscard]] constexpr std::uint8_t a() const { return argb >> 24; }
  [[nodiscard]] constexpr std::uint8_t r() const { return (argb >> 16) & 0xFF; }
  [[nodiscard]] constexpr std::uint8_t g() const { return (argb >> 8) & 0xFF; }
  [[nodiscard]] constexpr std::uint8_t b() const { return argb & 0xFF; }

  [[nodiscard]] static constexpr Color FromArgb(std::uint8_t a,
                                                std::uint8_t r,
                                                std::uint8_t g,
                                                std::uint8_t b) {
    return {static_cast<std::uint32_t>(a) << 24 |
            static_cast<std::uint32_t>(r) << 16 |
            static_cast<std::uint32_t>(g) << 8 | b};
  }

  [[nodiscard]] constexpr Color WithAlpha(std::uint8_t alpha) const {
    return {(argb & 0x00FFFFFFu) | static_cast<std::uint32_t>(alpha) << 24};
  }

  friend constexpr bool operator==(Color, Color) = default;
};

// Linear blend from `from` (t = 0) to `to` (t = 1), channel by channel.
[[nodiscard]] constexpr Color Mix(Color from, Color to, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  auto lerp = [t](std::uint8_t a, std::uint8_t b) {
    return static_cast<std::uint8_t>(
        std::lround(static_cast<float>(a) + (static_cast<float>(b) - a) * t));
  };
  return Color::FromArgb(lerp(from.a(), to.a()), lerp(from.r(), to.r()),
                         lerp(from.g(), to.g()), lerp(from.b(), to.b()));
}

}  // namespace kr
