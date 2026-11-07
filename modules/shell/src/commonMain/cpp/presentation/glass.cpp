#include "krypton/ui/model/glass.hpp"

#include <algorithm>
#include <cmath>

namespace kr {
GlassCurve MakeGlassCurve(float bezel,
                          float thickness,
                          float refractive_index,
                          float displacement_limit,
                          GlassSurfaceProfile profile) noexcept {
  GlassCurve result{};
  if (!std::isfinite(bezel) || !std::isfinite(thickness) ||
      !std::isfinite(refractive_index) || !std::isfinite(displacement_limit) ||
      bezel <= 0 || thickness < 0 || refractive_index <= 1 ||
      displacement_limit <= 0)
    return result;
  const float eta = 1 / refractive_index;
  for (std::size_t i = 0; i < result.size(); ++i) {
    const float t =
        static_cast<float>(i) / static_cast<float>(result.size() - 1);
    const float side = 1 - t;
    float nx = -side;
    float height = std::sqrt(std::max(0.0f, 1 - side * side));
    float ny = -height;
    if (profile == GlassSurfaceProfile::kConvexSquircle) {
      const float squared = side * side;
      height = std::sqrt(std::sqrt(std::max(0.0f, 1 - squared * squared)));
      // Normalize the derivative's numerator and denominator directly so the
      // steep outer endpoint stays finite, without a division by height.
      const float horizontal = side * squared;
      const float vertical = height * height * height;
      const float length = std::hypot(horizontal, vertical);
      nx = -horizontal / length;
      ny = -vertical / length;
    }
    const float q =
        eta * ny + std::sqrt(std::max(0.0f, 1 - eta * eta * (1 - ny * ny)));
    const float rx = -q * nx;
    const float ry = eta - q * ny;
    result[i] =
        std::clamp(rx * (height * bezel + thickness) / std::max(ry, 0.0001f),
                   0.0f, displacement_limit);
  }
  return result;
}
}  // namespace kr
