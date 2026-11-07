#pragma once

#include <algorithm>
#include <format>

namespace kr {

// All geometry in commonMain is in DIPs. Platform code converts to physical
// pixels only at the point a native API demands them.

struct Point {
  float x = 0;
  float y = 0;

  friend constexpr bool operator==(const Point&, const Point&) = default;
};

struct Rect {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;

  [[nodiscard]] constexpr float right() const { return x + width; }
  [[nodiscard]] constexpr float bottom() const { return y + height; }
  [[nodiscard]] constexpr bool empty() const {
    return width <= 0 || height <= 0;
  }

  [[nodiscard]] constexpr bool Contains(Point p) const {
    return p.x >= x && p.y >= y && p.x < right() && p.y < bottom();
  }

  [[nodiscard]] constexpr Rect Inset(float left,
                                     float top,
                                     float right_inset,
                                     float bottom_inset) const {
    return {x + left, y + top, std::max(0.0f, width - left - right_inset),
            std::max(0.0f, height - top - bottom_inset)};
  }

  [[nodiscard]] constexpr Rect Inset(float all) const {
    return Inset(all, all, all, all);
  }

  [[nodiscard]] constexpr Rect Offset(float dx, float dy) const {
    return {x + dx, y + dy, width, height};
  }

  friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

}  // namespace kr

template <>
struct std::formatter<kr::Rect> : std::formatter<float> {
  auto format(const kr::Rect& r, std::format_context& ctx) const {
    return std::format_to(ctx.out(), "{{x={} y={} w={} h={}}}", r.x, r.y,
                          r.width, r.height);
  }
};
