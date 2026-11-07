#pragma once

#include <span>

#include "krypton/ui/core/geometry.hpp"

namespace kr {

enum class FrameHit {
  kClient,
  kCaption,
  kLeft,
  kRight,
  kTop,
  kBottom,
  kTopLeft,
  kTopRight,
  kBottomLeft,
  kBottomRight,
};

// Cached client geometry in DIPs. The native layer supplies system resize
// extents and subtracts measured controls from the draggable rectangles.
struct FrameGeometry {
  Rect client;
  float resize_x = 0;
  float resize_y = 0;
  bool resizable = false;
  bool fullscreen = false;
};

[[nodiscard]] constexpr FrameHit HitTestFrame(
    const FrameGeometry& frame,
    Point point,
    std::span<const Rect> draggable) noexcept {
  if (frame.fullscreen || !frame.client.Contains(point))
    return FrameHit::kClient;
  if (frame.resizable) {
    const bool left = point.x < frame.client.x + frame.resize_x;
    const bool right = point.x >= frame.client.right() - frame.resize_x;
    const bool top = point.y < frame.client.y + frame.resize_y;
    const bool bottom = point.y >= frame.client.bottom() - frame.resize_y;
    if (top)
      return left    ? FrameHit::kTopLeft
             : right ? FrameHit::kTopRight
                     : FrameHit::kTop;
    if (bottom)
      return left    ? FrameHit::kBottomLeft
             : right ? FrameHit::kBottomRight
                     : FrameHit::kBottom;
    if (left)
      return FrameHit::kLeft;
    if (right)
      return FrameHit::kRight;
  }
  for (const auto& region : draggable)
    if (region.Contains(point))
      return FrameHit::kCaption;
  return FrameHit::kClient;
}

}  // namespace kr
