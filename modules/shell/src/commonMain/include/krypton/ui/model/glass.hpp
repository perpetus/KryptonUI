#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace kr {
enum class GlassMode : std::uint32_t { kOff, kPerformance, kQuality };
enum class GlassRole : std::uint32_t {
  kAddress,
  kActiveTab,
  kEssential,
  kPanel,
  kMenu,
  kSearch
};

struct GlassAvailability {
  bool hardware = true;
  bool transparency = true;
  bool high_contrast = false;
  bool energy_saver = false;
};

// Performance is a static tint and rim: no GPU passes, backdrop copies or
// refraction. Quality makes the steady chrome transparent over the system
// backdrop and refracts the live page only behind surfaces that float over it.
[[nodiscard]] constexpr GlassMode ResolveGlassMode(
    GlassMode requested,
    GlassAvailability available) noexcept {
  if (requested == GlassMode::kOff || !available.hardware ||
      !available.transparency || available.high_contrast)
    return GlassMode::kOff;
  return requested == GlassMode::kQuality && !available.energy_saver
             ? GlassMode::kQuality
             : GlassMode::kPerformance;
}

[[nodiscard]] constexpr bool GlassEligible(GlassMode mode,
                                           GlassRole role) noexcept {
  return mode == GlassMode::kQuality ||
         (mode == GlassMode::kPerformance &&
          (role <= GlassRole::kEssential || role == GlassRole::kMenu ||
           role == GlassRole::kSearch));
}

[[nodiscard]] constexpr bool GlassUsesPanelProfile(GlassRole role) noexcept {
  return role == GlassRole::kPanel || role == GlassRole::kMenu ||
         role == GlassRole::kSearch;
}

// Only Quality surfaces that float over the page refract a live host copy.
// Steady chrome sits over the system backdrop, which no process can sample.
[[nodiscard]] constexpr bool GlassUsesLiveBackdrop(GlassMode mode,
                                                   GlassRole role) noexcept {
  return mode == GlassMode::kQuality && GlassUsesPanelProfile(role);
}

// Quality leaves the host frame and chrome islands unpainted so the
// platform's system backdrop shows through.
[[nodiscard]] constexpr bool GlassTransparentChrome(GlassMode mode) noexcept {
  return mode == GlassMode::kQuality;
}

inline constexpr std::size_t kGlassCurveSamples = 256;
using GlassCurve = std::array<float, kGlassCurveSamples>;
enum class GlassSurfaceProfile { kConvexCircle, kConvexSquircle };

// A vertical incident ray through a convex bezel. Snell's law is
// evaluated once per profile, never in the pixel shader. Values are DIPs.
[[nodiscard]] GlassCurve MakeGlassCurve(
    float bezel,
    float thickness,
    float refractive_index,
    float displacement_limit,
    GlassSurfaceProfile profile = GlassSurfaceProfile::kConvexCircle) noexcept;
}  // namespace kr
