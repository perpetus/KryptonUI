#include "icon_view.hpp"

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <algorithm>
#include <chrono>
#include <format>

#include "native_motion.hpp"
#include "native_theme.hpp"

namespace kr::win {
namespace {
namespace packs = design::icon_packs;
constexpr auto& kSwap = design::kTokens.icon_packs;

const packs::PackIcon& Lookup(IconPack pack, IconRole role) noexcept {
  return packs::kIcons[static_cast<std::size_t>(pack)]
                      [static_cast<std::size_t>(role)];
}

xaml::Shapes::Path Parse(std::wstring_view data) {
  const auto markup = std::format(
      LR"(<Path xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Data="{}" />)",
      data);
  return xaml::Markup::XamlReader::Load(markup).as<xaml::Shapes::Path>();
}
}  // namespace

static_assert(packs::kPackCount == kIconPackCount);

void IconViewDeleter::operator()(IconView* view) const noexcept {
  delete view;
}

bool IconSet::Animates() const noexcept {
  return motion_.Enabled(brushes_.high_contrast());
}

void IconSet::SetPack(IconPack pack, bool animate) {
  if (static_cast<std::size_t>(pack) >= packs::kPackCount)
    return;
  const bool changed = pack != pack_;
  pack_ = pack;
  animate = animate && Animates();
  if (!changed && animate)
    return;
  for (auto* icon : icons_)
    icon->Apply(animate);
}

IconView::IconView(IconSet& set,
                   IconRole role,
                   double size,
                   const media::Brush& brush) try
    : set_(set), role_(role), size_(size), brush_(brush) {
  std::string_view stage = "root properties";
  try {
    root_.Width(size);
    root_.Height(size);
    root_.IsHitTestVisible(false);
    root_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    root_.VerticalAlignment(xaml::VerticalAlignment::Center);
    stage = "create layer";
    layers_[0] = MakeLayer();
    stage = "attach layer";
    root_.Children().Append(layers_[0].box);
    stage = "paint layer";
    Apply(false);
    set_.icons_.push_back(this);
  } catch (const winrt::hresult_error& error) {
    throw winrt::hresult_error(
        error.code(), winrt::to_hstring(std::format(
                          "{}: {}", stage, winrt::to_string(error.message()))));
  }
} catch (const winrt::hresult_error& error) {
  throw winrt::hresult_error(
      error.code(),
      winrt::to_hstring(std::format(
          "Chrome icon (pack {}, role {}, size {}): {}",
          static_cast<std::size_t>(set.pack()), static_cast<std::size_t>(role),
          size, winrt::to_string(error.message()))));
}

IconView::~IconView() {
  std::erase(set_.icons_, this);
}

IconView::Layer IconView::MakeLayer() const {
  Layer layer;
  layer.box = controls::Viewbox();
  layer.box.Width(size_);
  layer.box.Height(size_);
  layer.box.Stretch(media::Stretch::Uniform);
  layer.canvas = controls::Canvas();
  layer.box.Child(layer.canvas);
  return layer;
}

void IconView::Paint(Layer& layer) {
  std::string_view stage = "canvas size";
  try {
    const auto& icon = *layer.icon;
    layer.canvas.Width(icon.width);
    layer.canvas.Height(icon.height);
    stage = "parse native path";
    auto cached = std::ranges::find(paths_, layer.icon, &CachedPath::icon);
    if (cached == paths_.end()) {
      paths_.push_back({layer.icon, Parse(icon.data)});
      cached = paths_.end() - 1;
    }
    if (layer.path != cached->path) {
      stage = "attach native path";
      layer.canvas.Children().Clear();
      layer.path = cached->path;
      layer.canvas.Children().Append(layer.path);
    }
    if (icon.stroke > 0) {
      layer.path.StrokeThickness(icon.stroke);
      layer.path.StrokeStartLineCap(media::PenLineCap::Round);
      layer.path.StrokeEndLineCap(media::PenLineCap::Round);
      layer.path.StrokeLineJoin(media::PenLineJoin::Round);
    }
    stage = "paint native path";
    PaintBrush(layer);
  } catch (const winrt::hresult_error& error) {
    throw winrt::hresult_error(
        error.code(), winrt::to_hstring(std::format(
                          "{}: {}", stage, winrt::to_string(error.message()))));
  }
}

void IconView::PaintBrush(Layer& layer) const {
  if (layer.icon->stroke > 0) {
    layer.path.Stroke(brush_);
  } else {
    layer.path.Fill(brush_);
  }
}

void IconView::SetRole(IconRole role) {
  if (role == role_)
    return;
  role_ = role;
  Apply(set_.Animates());
}

void IconView::SetBrush(const media::Brush& brush) {
  if (brush == brush_)
    return;
  brush_ = brush;
  for (auto& layer : layers_)
    if (layer.icon)
      PaintBrush(layer);
}

void IconView::Settle() {
  for (std::size_t index = 0; index < layers_.size(); ++index) {
    auto& layer = layers_[index];
    if (!layer.box)
      continue;
    if (layer.fade)
      layer.box.StopAnimation(layer.fade);
    if (layer.scale)
      layer.box.StopAnimation(layer.scale);
    const bool visible = index == front_;
    layer.box.Opacity(visible ? 1.0 : 0.0);
    const float scale = visible ? 1.0f : kSwap.swap_scale;
    layer.box.Scale({scale, scale, 1});
  }
}

void IconView::Apply(bool animate) {
  // Initial shapes have nothing visible to crossfade. Keep construction off
  // the compositor and retain the second layer only after an actual swap.
  animate = animate && root_.IsLoaded();
  const auto* icon = &Lookup(set_.pack(), role_);
  auto& front = layers_[front_];
  if (!animate && layers_[1].box)
    Settle();
  if (front.icon == icon)
    return;
  if (!animate || !front.icon) {
    front.icon = icon;
    Paint(front);
    return;
  }
  // Crossfade: the old shape shrinks and fades as the new one grows in.
  const auto back_index = 1 - front_;
  auto& back = layers_[back_index];
  if (!back.box) {
    back = MakeLayer();
    root_.Children().Append(back.box);
  }
  back.icon = icon;
  Paint(back);
  const auto compositor =
      xaml::Hosting::ElementCompositionPreview::GetElementVisual(root_)
          .Compositor();
  const auto duration =
      std::chrono::milliseconds(static_cast<int>(kSwap.swap_ms));
  const auto easing = compositor.CreateCubicBezierEasingFunction(
      {kSwap.bezier[0], kSwap.bezier[1]}, {kSwap.bezier[2], kSwap.bezier[3]});
  const auto center = winrt::Windows::Foundation::Numerics::float3{
      static_cast<float>(size_) / 2, static_cast<float>(size_) / 2, 0};
  const auto run = [&](Layer& layer, float from_opacity, float to_opacity,
                       float from_scale, float to_scale) {
    const auto& box = layer.box;
    box.CenterPoint(center);
    if (!layer.fade)
      layer.fade = compositor.CreateScalarKeyFrameAnimation();
    const auto& fade = layer.fade;
    fade.Target(L"Opacity");
    fade.InsertKeyFrame(0, from_opacity);
    fade.InsertKeyFrame(1, to_opacity, easing);
    fade.Duration(duration);
    if (!layer.scale)
      layer.scale = compositor.CreateVector3KeyFrameAnimation();
    const auto& scale = layer.scale;
    scale.Target(L"Scale");
    scale.InsertKeyFrame(0, {from_scale, from_scale, 1});
    scale.InsertKeyFrame(1, {to_scale, to_scale, 1}, easing);
    scale.Duration(duration);
    box.Opacity(to_opacity);
    box.Scale({to_scale, to_scale, 1});
    box.StartAnimation(fade);
    box.StartAnimation(scale);
  };
  run(back, 0, 1, kSwap.swap_scale, 1);
  run(front, 1, 0, 1, kSwap.swap_scale);
  front_ = back_index;
}

std::unique_ptr<NativeButton> PackButton(IconSet& icons,
                                         const Brushes& brushes,
                                         IconRole role,
                                         std::wstring_view name,
                                         Task action) {
  auto result = IconButton(brushes, L"", name, std::move(action));
  result->icon.reset(
      new IconView(icons, role, kTokens.spacing.icon_size, brushes.icon));
  result->control.Content(result->icon->Root());
  return result;
}
}  // namespace kr::win
