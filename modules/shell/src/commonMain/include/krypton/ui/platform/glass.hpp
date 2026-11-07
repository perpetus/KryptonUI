#pragma once

#include <cstdint>
#include <span>

#include "krypton/ui/core/geometry.hpp"

namespace kr {
// Native handles are interpreted only by the platform actual. They are borrowed
// during registration; the actual duplicates them before returning success.
struct GlassBackdropResource {
  std::uint64_t id = 0;
  std::uintptr_t texture = 0, ready_fence = 0, release_fence = 0;
  std::uint64_t adapter = 0;
  std::uint32_t width = 0, height = 0;
  Rect bounds;
};
struct GlassBackdropFrame {
  std::uint64_t resource = 0, revision = 0, ready = 0, release = 0;
};
struct GlassBackdropRequest {
  std::uint64_t revision = 0;
  std::span<const Rect> regions;
  float scale = 1, margin = 0;
  std::uint64_t adapter = 0;
};
}  // namespace kr
