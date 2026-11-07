// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Shapes and transitions adapted from Zen's ZenFolder.mjs and
// zen-folder-icon.inc.css in reference/desktop/src/zen/folders/.
#include "folder_icon.hpp"

#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <numbers>

namespace kr::win {
namespace composition = winrt::Microsoft::UI::Composition;
namespace shapes = xaml::Shapes;
namespace {
// Coordinates belong to the original 27x27 vector asset.
constexpr std::wstring_view kFolder =
    LR"(
<Canvas xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
  <Canvas x:Name="Back">
    <Path x:Name="BackFill" Data="M8 5.625H11.9473C12.4866 5.625 13.0105 5.80861 13.4316 6.14551L14.2881 6.83105C14.9308 7.34508 15.7298 7.625 16.5527 7.625H20C21.3117 7.625 22.375 8.68832 22.375 10V20C22.375 21.3117 21.3117 22.375 20 22.375H8C6.68832 22.375 5.625 21.3117 5.625 20V8C5.625 6.68832 6.68832 5.625 8 5.625Z"/>
    <Path x:Name="BackShade" Data="M8 5.625H11.9473C12.4866 5.625 13.0105 5.80861 13.4316 6.14551L14.2881 6.83105C14.9308 7.34508 15.7298 7.625 16.5527 7.625H20C21.3117 7.625 22.375 8.68832 22.375 10V20C22.375 21.3117 21.3117 22.375 20 22.375H8C6.68832 22.375 5.625 21.3117 5.625 20V8C5.625 6.68832 6.68832 5.625 8 5.625Z"/>
  </Canvas>
  <Canvas x:Name="Front">
    <Rectangle x:Name="FrontFill" Canvas.Left="5.625" Canvas.Top="9.625" Width="16.75" Height="12.75" RadiusX="2.375" RadiusY="2.375"/>
    <Rectangle x:Name="FrontShade" Canvas.Left="5.625" Canvas.Top="9.625" Width="16.75" Height="12.75" RadiusX="2.375" RadiusY="2.375"/>
    <TextBlock x:Name="Emblem" Canvas.Left="8.5" Canvas.Top="10.5" TextAlignment="Center"/>
    <Canvas x:Name="Dots">
      <Ellipse Canvas.Left="8.75" Canvas.Top="14.75" Width="2.5" Height="2.5"/>
      <Ellipse Canvas.Left="12.75" Canvas.Top="14.75" Width="2.5" Height="2.5"/>
      <Ellipse Canvas.Left="16.75" Canvas.Top="14.75" Width="2.5" Height="2.5"/>
    </Canvas>
  </Canvas>
</Canvas>)";
}  // namespace

FolderIcon::FolderIcon() {
  const auto& t = kTokens.folder;
  canvas_ = xaml::Markup::XamlReader::Load(kFolder).as<controls::Canvas>();
  canvas_.Width(t.view_box);
  canvas_.Height(t.view_box);
  root_ = controls::Viewbox();
  root_.Width(t.size);
  root_.Height(t.size);
  root_.IsHitTestVisible(false);
  root_.Child(canvas_);
  back_ = canvas_.FindName(L"Back").as<controls::Canvas>();
  front_ = canvas_.FindName(L"Front").as<controls::Canvas>();
  emblem_ = canvas_.FindName(L"Emblem").as<controls::TextBlock>();
  emblem_.FontFamily(
      xaml::Media::FontFamily(winrt::to_hstring(kTokens.type.family)));
  emblem_.FontSize(t.glyph_size);
  emblem_.Width(t.glyph_size);
  emblem_.Height(t.glyph_size);
  dots_ = canvas_.FindName(L"Dots").as<xaml::UIElement>();
  back_visual_ =
      xaml::Hosting::ElementCompositionPreview::GetElementVisual(back_);
  front_visual_ =
      xaml::Hosting::ElementCompositionPreview::GetElementVisual(front_);
  emblem_visual_ =
      xaml::Hosting::ElementCompositionPreview::GetElementVisual(emblem_);
  dots_visual_ =
      xaml::Hosting::ElementCompositionPreview::GetElementVisual(dots_);
  const auto compositor = back_visual_.Compositor();
  progress_ = compositor.CreatePropertySet();
  progress_.InsertScalar(L"Open", 0);
  progress_.InsertScalar(L"Shrink", 1 - t.open_scale);
  progress_.InsertScalar(L"Skew",
                         t.open_skew * std::numbers::pi_v<float> / 180);
  progress_.InsertScalar(L"Y", t.open_y);
  // CSS applies scale, translation, then skew, around the SVG origin. The
  // expression preserves that order, including the skewed Y translation.
  const auto bind = [&](const composition::Visual& visual, bool back) {
    const auto transform = compositor.CreateExpressionAnimation(
        L"Matrix4x4(1-p.Open*p.Shrink,0,0,0,"
        L"side*Tan(p.Open*p.Skew)*(1-p.Open*p.Shrink),1-p.Open*p.Shrink,0,0,"
        L"0,0,1,0,p.Open*x+side*Tan(p.Open*p.Skew)*p.Open*p.Y,p.Open*p.Y,0,1)");
    transform.SetReferenceParameter(L"p", progress_);
    transform.SetScalarParameter(L"side", back ? 1.0f : -1.0f);
    transform.SetScalarParameter(L"x", back ? t.back_x : t.front_x);
    visual.StartAnimation(L"TransformMatrix", transform);
  };
  bind(back_visual_, true);
  bind(front_visual_, false);
  animation_ = compositor.CreateScalarKeyFrameAnimation();
  animation_.Duration(Ms(t.motion_ms));
  animation_.InsertExpressionKeyFrame(0, L"this.StartingValue");
  opacity_ = compositor.CreateScalarKeyFrameAnimation();
  opacity_.Duration(Ms(t.motion_ms));
  opacity_.InsertExpressionKeyFrame(0, L"this.StartingValue");
  easing_ = Bezier(compositor, t.easing);
}

FolderIcon::~FolderIcon() {
  Reset();
  try {
    back_visual_.StopAnimation(L"TransformMatrix");
    front_visual_.StopAnimation(L"TransformMatrix");
  } catch (...) {
    // The island can release its composition tree before retained rows.
  }
}
void FolderIcon::Reset() noexcept {
  try {
    progress_.StopAnimation(L"Open");
    emblem_visual_.StopAnimation(L"Opacity");
    dots_visual_.StopAnimation(L"Opacity");
  } catch (...) {
    // Recycling during native window teardown may follow tree disposal.
  }
  initialized_ = false;
}

void FolderIcon::Update(bool open,
                        bool active,
                        std::string_view glyph,
                        const ThemeVm& theme,
                        Brushes& brushes,
                        const NativeMotion& motion,
                        bool animate) {
  const auto& t = kTokens.folder;
  const bool contrast = brushes.high_contrast();
  if (!theme_ready_ || dark_ != theme.dark || contrast_ != contrast ||
      accent_ != theme.accent) {
    const auto rear =
        contrast
            ? brushes.pill
            : brushes.For(Mix(Color{theme.dark ? t.back_dark : t.back_light},
                              theme.accent, t.back_accent));
    const auto front =
        contrast ? brushes.pill
                 : brushes.For(
                       Mix(theme.accent, Color{theme.dark ? t.black : t.white},
                           theme.dark ? t.front_dark_mix : t.front_light_mix));
    const auto stroke =
        contrast ? brushes.text
                 : brushes.For(theme.dark ? Mix(Color{t.stroke_dark},
                                                theme.accent, t.stroke_dark_mix)
                                          : Mix(theme.accent, Color{t.black},
                                                t.stroke_light_mix));
    canvas_.FindName(L"BackFill").as<shapes::Path>().Fill(rear);
    canvas_.FindName(L"FrontFill").as<shapes::Rectangle>().Fill(front);
    for (const auto name : {L"BackShade", L"FrontShade"}) {
      const auto shape = canvas_.FindName(name).as<shapes::Shape>();
      shape.Stroke(stroke);
      shape.StrokeThickness(t.stroke_width);
      // Retain the gradient with each layer instead of allocating on updates.
      if (!shape.Fill()) {
        xaml::Media::LinearGradientBrush shade;
        shade.StartPoint({0, 0});
        shade.EndPoint({0, 1});
        xaml::Media::GradientStop white, black;
        white.Color(NativeColor(Color{t.white}));
        white.Offset(0);
        black.Color(NativeColor(Color{t.black}));
        black.Offset(1);
        shade.GradientStops().Append(white);
        shade.GradientStops().Append(black);
        shade.Opacity(t.shade_opacity);
        shape.Fill(shade);
      }
      shape.Fill().Opacity(contrast ? 0 : t.shade_opacity);
    }
    emblem_.Foreground(stroke);
    for (const auto child : dots_.as<controls::Canvas>().Children())
      child.as<shapes::Ellipse>().Fill(stroke);
    dark_ = theme.dark;
    contrast_ = contrast;
    accent_ = theme.accent;
    theme_ready_ = true;
  }
  if (glyph_ != glyph) {
    glyph_ = glyph;
    emblem_.Text(winrt::to_hstring(glyph));
  }
  const bool dots_visible = active && !open;
  const bool animated = motion.Enabled(contrast);
  if (!initialized_ || dots_visible_ != dots_visible || !animated) {
    const auto fade = [&](const composition::Visual& visual, bool visible) {
      if (initialized_ && animate && animated) {
        opacity_.InsertKeyFrame(1, visible ? 1.0f : 0.0f, easing_);
        visual.StartAnimation(L"Opacity", opacity_);
      } else {
        visual.StopAnimation(L"Opacity");
        visual.Opacity(visible ? 1.0f : 0.0f);
      }
    };
    fade(emblem_visual_, !dots_visible);
    fade(dots_visual_, dots_visible);
    dots_visible_ = dots_visible;
  }
  if (!initialized_ || open_ != open || !animated) {
    if (initialized_ && animate && animated) {
      animation_.InsertKeyFrame(1, open ? 1.0f : 0.0f, easing_);
      progress_.StartAnimation(L"Open", animation_);
    } else {
      progress_.StopAnimation(L"Open");
      progress_.InsertScalar(L"Open", open ? 1.0f : 0.0f);
    }
    open_ = open;
    initialized_ = true;
  }
}
}  // namespace kr::win
