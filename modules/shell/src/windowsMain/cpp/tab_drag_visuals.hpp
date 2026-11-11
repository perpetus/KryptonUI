#pragma once

#include <winrt/Microsoft.UI.Composition.h>

#include "view_context.hpp"

namespace kr::win {

// What a sidebar drag shows: Zen's drop indicator and the dragged item's
// image. Both are composition children of the list, so following the pointer
// changes offsets without a layout pass. Created on the first drag and kept.
class TabDragVisuals {
 public:
  explicit TabDragVisuals(xaml::UIElement host);
  ~TabDragVisuals();
  TabDragVisuals(const TabDragVisuals&) = delete;
  TabDragVisuals& operator=(const TabDragVisuals&) = delete;

  // A live image of `element`, drawn over `fill` and grabbed at `grab`.
  struct Row {
    xaml::UIElement element{nullptr};
    float width = 0;
    float height = 0;
    Point grab;
    Color fill;
    float radius = 0;
  };
  // An Essential tile: `icon` centred on `fill`.
  struct Tile {
    xaml::UIElement icon{nullptr};
    float icon_size = 0;
    float width = 0;
    float height = 0;
    Color fill;
    float radius = 0;
  };

  // Starts showing the dragged row, or the tile when `row` has no element.
  // `tile_grab` places a tile relative to the pointer.
  void Begin(const Row& row, const Tile& tile, Point tile_grab, Point pointer);
  void Move(Point pointer);
  // Shows the tile, `width` by `height`, instead of the row; or the row.
  void ShowTile(bool tile, float width, float height);
  // The row's element now shows another item: keep only its backdrop.
  void DetachRow();
  // A line across `slot` at `at` (a vertical one in a horizontal strip).
  void ShowIndicator(Rect slot, float at, bool vertical, Color color);
  void HideIndicator();
  void End();

 private:
  void Ensure();
  void Place();
  xaml::UIElement host_;
  winrt::Microsoft::UI::Composition::Compositor compositor_{nullptr};
  winrt::Microsoft::UI::Composition::ContainerVisual root_{nullptr};
  winrt::Microsoft::UI::Composition::ShapeVisual indicator_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionColorBrush indicator_brush_{
      nullptr};
  winrt::Microsoft::UI::Composition::CompositionRoundedRectangleGeometry line_{
      nullptr};
  winrt::Microsoft::UI::Composition::CompositionEllipseGeometry ring_{nullptr};
  winrt::Microsoft::UI::Composition::ContainerVisual image_{nullptr};
  winrt::Microsoft::UI::Composition::ContainerVisual row_{nullptr};
  winrt::Microsoft::UI::Composition::ShapeVisual row_back_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionColorBrush row_fill_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionRoundedRectangleGeometry
      row_shape_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionVisualSurface row_surface_{
      nullptr};
  winrt::Microsoft::UI::Composition::SpriteVisual row_content_{nullptr};
  winrt::Microsoft::UI::Composition::ContainerVisual tile_{nullptr};
  winrt::Microsoft::UI::Composition::ShapeVisual tile_back_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionColorBrush tile_fill_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionRoundedRectangleGeometry
      tile_shape_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionVisualSurface tile_surface_{
      nullptr};
  winrt::Microsoft::UI::Composition::SpriteVisual tile_icon_{nullptr};
  Point pointer_;
  Point row_grab_;
  Point tile_grab_;
  float icon_size_ = 0;
  bool tile_shown_ = false;
};

}  // namespace kr::win
