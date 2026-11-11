#include "tab_drag_visuals.hpp"

#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include <algorithm>

#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
namespace {
namespace composition = winrt::Microsoft::UI::Composition;
namespace hosting = xaml::Hosting;
using winrt::Windows::Foundation::Numerics::float2;
constexpr const auto& kDrag = kTokens.tab_drag;
}  // namespace

TabDragVisuals::TabDragVisuals(xaml::UIElement host) : host_(std::move(host)) {}

TabDragVisuals::~TabDragVisuals() {
  if (!root_)
    return;
  try {
    hosting::ElementCompositionPreview::SetElementChildVisual(host_, nullptr);
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Tab drag visuals could not detach");
  }
}

void TabDragVisuals::Ensure() {
  if (root_)
    return;
  compositor_ = media::CompositionTarget::GetCompositorForCurrentThread();
  root_ = compositor_.CreateContainerVisual();

  indicator_ = compositor_.CreateShapeVisual();
  indicator_brush_ = compositor_.CreateColorBrush();
  line_ = compositor_.CreateRoundedRectangleGeometry();
  const auto line = compositor_.CreateSpriteShape(line_);
  line.FillBrush(indicator_brush_);
  ring_ = compositor_.CreateEllipseGeometry();
  const auto ring = compositor_.CreateSpriteShape(ring_);
  ring.StrokeBrush(indicator_brush_);
  ring.StrokeThickness(kDrag.indicator_thickness);
  indicator_.Shapes().Append(line);
  indicator_.Shapes().Append(ring);
  indicator_.IsVisible(false);

  const auto backdrop =
      [this](composition::ShapeVisual& visual,
             composition::CompositionColorBrush& fill,
             composition::CompositionRoundedRectangleGeometry& shape) {
        visual = compositor_.CreateShapeVisual();
        fill = compositor_.CreateColorBrush();
        shape = compositor_.CreateRoundedRectangleGeometry();
        const auto sprite = compositor_.CreateSpriteShape(shape);
        sprite.FillBrush(fill);
        visual.Shapes().Append(sprite);
      };
  row_ = compositor_.CreateContainerVisual();
  backdrop(row_back_, row_fill_, row_shape_);
  row_surface_ = compositor_.CreateVisualSurface();
  row_content_ = compositor_.CreateSpriteVisual();
  row_content_.Brush(compositor_.CreateSurfaceBrush(row_surface_));
  row_.Children().InsertAtTop(row_back_);
  row_.Children().InsertAtTop(row_content_);

  tile_ = compositor_.CreateContainerVisual();
  backdrop(tile_back_, tile_fill_, tile_shape_);
  tile_surface_ = compositor_.CreateVisualSurface();
  tile_icon_ = compositor_.CreateSpriteVisual();
  tile_icon_.Brush(compositor_.CreateSurfaceBrush(tile_surface_));
  tile_.Children().InsertAtTop(tile_back_);
  tile_.Children().InsertAtTop(tile_icon_);

  image_ = compositor_.CreateContainerVisual();
  image_.Opacity(kDrag.ghost_opacity);
  image_.Children().InsertAtTop(row_);
  image_.Children().InsertAtTop(tile_);
  image_.IsVisible(false);

  root_.Children().InsertAtTop(indicator_);
  root_.Children().InsertAtTop(image_);
  hosting::ElementCompositionPreview::SetElementChildVisual(host_, root_);
}

void TabDragVisuals::Begin(const Row& row,
                           const Tile& tile,
                           Point tile_grab,
                           Point pointer) {
  Ensure();
  const bool has_row = static_cast<bool>(row.element);
  if (has_row) {
    const float2 size{row.width, row.height};
    row_surface_.SourceVisual(
        hosting::ElementCompositionPreview::GetElementVisual(row.element));
    row_surface_.SourceSize(size);
    row_content_.Size(size);
    row_content_.IsVisible(true);
    row_back_.Size(size);
    row_shape_.Size(size);
    row_shape_.CornerRadius({row.radius, row.radius});
    row_fill_.Color(NativeColor(row.fill));
  }
  row_grab_ = row.grab;
  icon_size_ = tile.icon_size;
  if (tile.icon) {
    tile_surface_.SourceVisual(
        hosting::ElementCompositionPreview::GetElementVisual(tile.icon));
    tile_surface_.SourceSize({tile.icon_size, tile.icon_size});
    tile_icon_.Size({tile.icon_size, tile.icon_size});
    tile_icon_.IsVisible(true);
  } else {
    tile_surface_.SourceVisual(nullptr);
    tile_icon_.IsVisible(false);
  }
  tile_fill_.Color(NativeColor(tile.fill));
  tile_shape_.CornerRadius({tile.radius, tile.radius});
  tile_grab_ = tile_grab;
  pointer_ = pointer;
  tile_shown_ = has_row;
  ShowTile(!has_row, tile.width, tile.height);
  Place();
  image_.IsVisible(true);
}

void TabDragVisuals::ShowTile(bool tile, float width, float height) {
  if (!image_)
    return;
  if (tile) {
    const float2 size{width, height};
    tile_back_.Size(size);
    tile_shape_.Size(size);
    tile_icon_.Offset({(width - icon_size_) / 2, (height - icon_size_) / 2, 0});
  }
  if (tile_shown_ == tile)
    return;
  tile_shown_ = tile;
  row_.IsVisible(!tile);
  tile_.IsVisible(tile);
  Place();
}

void TabDragVisuals::DetachRow() {
  if (!row_surface_)
    return;
  row_surface_.SourceVisual(nullptr);
  row_content_.IsVisible(false);
}

void TabDragVisuals::Move(Point pointer) {
  pointer_ = pointer;
  if (image_)
    Place();
}

void TabDragVisuals::Place() {
  const auto grab = tile_shown_ ? tile_grab_ : row_grab_;
  image_.Offset({pointer_.x - grab.x, pointer_.y - grab.y, 0});
}

void TabDragVisuals::ShowIndicator(Rect slot,
                                   float at,
                                   bool vertical,
                                   Color color) {
  Ensure();
  // Zen lays the line half its separation inside the row and starts it past
  // the ring. The ring's outer edge is two line widths from its centre.
  const float thickness = kDrag.indicator_thickness;
  const float start = (vertical ? slot.y : slot.x) +
                      kDrag.indicator_separation / 2 + 2 * thickness +
                      kDrag.indicator_gap;
  const float end = (vertical ? slot.bottom() : slot.right()) -
                    kDrag.indicator_separation / 2;
  const float length = std::max(0.0f, end - start);
  const float radius = std::min(kDrag.indicator_radius, thickness / 2);
  line_.CornerRadius({radius, radius});
  if (vertical) {
    line_.Offset({at - thickness / 2, start});
    line_.Size({thickness, length});
    ring_.Center({at, start - thickness});
  } else {
    line_.Offset({start, at - thickness / 2});
    line_.Size({length, thickness});
    ring_.Center({start - thickness, at});
  }
  // The stroke is centred on the geometry: 1.5 widths gives an outer radius
  // of two widths around an inner one of one width.
  ring_.Radius({1.5f * thickness, 1.5f * thickness});
  indicator_brush_.Color(NativeColor(color));
  const auto size = host_.ActualSize();
  indicator_.Size({size.x, size.y});
  indicator_.IsVisible(true);
}

void TabDragVisuals::HideIndicator() {
  if (indicator_)
    indicator_.IsVisible(false);
}

void TabDragVisuals::End() {
  if (!root_)
    return;
  indicator_.IsVisible(false);
  image_.IsVisible(false);
  // Release the dragged elements' visuals until the next drag.
  row_surface_.SourceVisual(nullptr);
  tile_surface_.SourceVisual(nullptr);
}

}  // namespace kr::win
