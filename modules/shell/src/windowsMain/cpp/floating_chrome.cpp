#include "floating_chrome.hpp"

#include <winrt/Microsoft.UI.Xaml.Hosting.h>

namespace kr::win {
namespace hosting = xaml::Hosting;

void FloatingChrome::Initialize(const controls::Grid& surface) {
  surface_ = surface;
  shadow_host_.IsHitTestVisible(false);
  root_.Children().Append(shadow_host_);
  root_.Children().Append(surface_);
}

void FloatingChrome::EnsureShadow() {
  if (layer_)
    return;
  const auto compositor =
      hosting::ElementCompositionPreview::GetElementVisual(root_).Compositor();
  clip_ = compositor.CreateRoundedRectangleGeometry();
  surface_clip_ = compositor.CreateGeometricClip(clip_);
  layer_ = compositor.CreateLayerVisual();
  shape_ = compositor.CreateShapeVisual();
  outline_ = compositor.CreateRoundedRectangleGeometry();
  auto shape = compositor.CreateSpriteShape(outline_);
  fill_ = compositor.CreateColorBrush(
      NativeColor(Color{kTokens.compact.shadow_mask}));
  shape.FillBrush(fill_);
  shape_.Shapes().Append(shape);
  layer_.Children().InsertAtTop(shape_);
  shadow_ = compositor.CreateDropShadow();
  shadow_.BlurRadius(kTokens.compact.shadow_blur);
  shadow_.Offset({0, kTokens.compact.shadow_offset, 0});
  layer_.Shadow(shadow_);
  hosting::ElementCompositionPreview::SetElementChildVisual(shadow_host_,
                                                            layer_);
}

void FloatingChrome::Update(Rect surface,
                            Rect island,
                            bool floating,
                            bool dark,
                            bool high_contrast) {
  const xaml::Thickness margin{surface.x - island.x, surface.y - island.y,
                               island.right() - surface.right(),
                               island.bottom() - surface.bottom()};
  if (surface_.Margin() != margin)
    surface_.Margin(margin);
  const float radius = floating ? kTokens.compact.radius : 0;
  const auto corners = UniformCorners(radius);
  if (surface_.CornerRadius() != corners)
    surface_.CornerRadius(corners);
  surface_.BorderThickness(
      UniformThickness(floating ? kTokens.compact.border_width : 0));
  if (!floating) {
    if (surface_clip_)
      hosting::ElementCompositionPreview::GetElementVisual(surface_).Clip(
          nullptr);
    if (layer_)
      layer_.IsVisible(false);
    return;
  }
  EnsureShadow();
  // The shadow caster is also an opaque theme-colored floor beneath tinted
  // chrome. A white mask must never show through a translucent dark gradient.
  fill_.Color(
      NativeColor(Color{dark ? kTokens.dark.overlay : kTokens.light.overlay}));
  clip_.Size({surface.width, surface.height});
  clip_.CornerRadius({radius, radius});
  hosting::ElementCompositionPreview::GetElementVisual(surface_).Clip(
      surface_clip_);
  layer_.Size({island.width, island.height});
  layer_.Offset({surface.x - island.x, surface.y - island.y, 0});
  shape_.Size({surface.width, surface.height});
  outline_.Size({surface.width, surface.height});
  outline_.CornerRadius({radius, radius});
  shadow_.Color(NativeColor(Color{dark ? kTokens.compact.shadow_dark
                                       : kTokens.compact.shadow_light}));
  layer_.IsVisible(!high_contrast);
}

}  // namespace kr::win
