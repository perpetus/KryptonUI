#include "theme_canvas.hpp"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Windows.System.h>

#include <algorithm>
#include <cmath>

#include "krypton/ui/presentation/theme_editor.hpp"

namespace kr::win {
ThemeCanvas::ThemeCanvas(ViewContext& context) : context_(context) {
  const auto& t = kTokens.theme_editor;
  root_.IsTabStop(true);
  // The board keeps keyboard input without a persistent system focus frame.
  root_.UseSystemFocusVisuals(false);
  root_.BorderThickness(UniformThickness(0));
  root_.BorderBrush(context_.brushes.transparent);
  root_.Background(context_.brushes.transparent);
  root_.Padding(UniformThickness(0));
  root_.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
  root_.VerticalContentAlignment(xaml::VerticalAlignment::Stretch);
  root_.Content(board_);
  board_.Clip(clip_);
  board_.Background(context_.brushes.editor_board);
  board_.CornerRadius(
      UniformCorners(std::max(0.0f, t.radius - t.board_radius_inset)));
  pattern_.Stretch(media::Stretch::Fill);
  pattern_.IsHitTestVisible(false);
  board_.Children().Append(pattern_);
  points_.FlowDirection(xaml::FlowDirection::LeftToRight);
  board_.Children().Append(points_);
  hint_ = Label(L"", kTokens.type.size_small);
  hint_.Foreground(context_.brushes.editor_foreground);
  hint_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  hint_.VerticalAlignment(xaml::VerticalAlignment::Center);
  hint_.TextWrapping(xaml::TextWrapping::Wrap);
  hint_.IsHitTestVisible(false);
  board_.Children().Append(hint_);
  for (std::size_t i = 0; i < dots_.size(); ++i) {
    auto& dot = dots_[i];
    const float border = i ? t.secondary_border : t.primary_border;
    const float size = (i ? t.secondary_size : t.primary_size) + 2 * border;
    dot.Width(size);
    dot.Height(size);
    dot.StrokeThickness(border);
    dot.Stroke(context_.brushes.editor_point_border);
    dot.Fill(colors_[i]);
    dot.IsHitTestVisible(false);
    points_.Children().Append(dot);
  }
  sized_ = root_.SizeChanged(
      winrt::auto_revoke,
      [this](auto&&, const xaml::SizeChangedEventArgs& args) {
        const auto side = static_cast<float>(args.NewSize().Width);
        if (side > 0 && side_ != side) {
          CancelDrag();
          side_ = side;
          root_.Height(side);
          Arrange();
        }
      });
  pressed_ = root_.PointerPressed(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (side_ <= 0 || dragging_pointer_ || args.IsGenerated() ||
            !args.GetCurrentPoint(points_).Properties().IsLeftButtonPressed())
          return;
        root_.Focus(xaml::FocusState::Pointer);
        if (!root_.CapturePointer(args.Pointer()))
          return;
        dragging_pointer_ = args.Pointer().PointerId();
        drag_offset_ = {};
        const auto local = args.GetCurrentPoint(points_).Position();
        const Point position{local.X / side_, local.Y / side_};
        if (const auto hit = HitThemePoint(value_, position, side_)) {
          drag_offset_ = {value_.points[*hit].position.x - position.x,
                          value_.points[*hit].position.y - position.y};
          context_.delegate.OnThemeEdited(
              {.kind = ThemeEditKind::kSelectPoint, .index = *hit});
        }
        Move(args, true);
        args.Handled(true);
      });
  moved_ = root_.PointerMoved(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (dragging_pointer_ != args.Pointer().PointerId())
          return;
        args.Handled(true);
        if (!args.GetCurrentPoint(points_).Properties().IsLeftButtonPressed()) {
          dragging_pointer_.reset();
          root_.ReleasePointerCapture(args.Pointer());
          return;
        }
        Move(args);
      });
  released_ = root_.PointerReleased(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (dragging_pointer_ == args.Pointer().PointerId()) {
          Move(args);
          dragging_pointer_.reset();
          root_.ReleasePointerCapture(args.Pointer());
          args.Handled(true);
        }
      });
  capture_lost_ = root_.PointerCaptureLost(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (dragging_pointer_ == args.Pointer().PointerId())
          dragging_pointer_.reset();
      });
  cancelled_ = root_.PointerCanceled(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (dragging_pointer_ == args.Pointer().PointerId())
          CancelDrag();
      });
  key_ = root_.KeyDown(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::KeyRoutedEventArgs& args) {
        using winrt::Windows::System::VirtualKey;
        Point p =
            value_.point_count ? value_.points[0].position : Point{0.5f, 0.5f};
        const float step = kTokens.theme_editor.keyboard_step;
        switch (args.Key()) {
          case VirtualKey::Left:
            p.x -= step;
            break;
          case VirtualKey::Right:
            p.x += step;
            break;
          case VirtualKey::Up:
            p.y -= step;
            break;
          case VirtualKey::Down:
            p.y += step;
            break;
          case VirtualKey::Home:
            p = {0.5f, 0.5f};
            break;
          case VirtualKey::Enter:
            break;
          default:
            return;
        }
        context_.delegate.OnThemeEdited(
            {.kind = ThemeEditKind::kMovePrimary, .position = p});
        args.Handled(true);
      });
}
ThemeCanvas::~ThemeCanvas() {
  CancelDrag();
}
void ThemeCanvas::CancelDrag() noexcept {
  const bool captured = dragging_pointer_.has_value();
  dragging_pointer_.reset();
  try {
    if (captured)
      root_.ReleasePointerCaptures();
  } catch (...) {
  }
}

void ThemeCanvas::Move(const xaml::Input::PointerRoutedEventArgs& args,
                       bool initial) {
  if (side_ <= 0 || args.IsGenerated())
    return;
  // Layout can re-hit-test a stationary pointer while a theme preview renders.
  // Compare positions in the XamlRoot before reading the board's local space.
  const auto position = args.GetCurrentPoint(nullptr).Position();
  if (!initial && position == last_pointer_position_)
    return;
  last_pointer_position_ = position;
  const auto p = args.GetCurrentPoint(points_).Position();
  context_.delegate.OnThemeEdited({.kind = ThemeEditKind::kMovePrimary,
                                   .position = {p.X / side_ + drag_offset_.x,
                                                p.Y / side_ + drag_offset_.y}});
}

void ThemeCanvas::Arrange() {
  points_.Width(side_);
  points_.Height(side_);
  const foundation::Rect bounds{0, 0, side_, side_};
  if (clip_.Rect() != bounds)
    clip_.Rect(bounds);
  const auto color = context_.brushes.editor_dots.Color();
  const auto bitmap = pattern_cache_.Dots(
      static_cast<int>(std::ceil(side_)), static_cast<int>(std::ceil(side_)),
      Color::FromArgb(color.A, color.R, color.G, color.B));
  if (pattern_.Source() != bitmap)
    pattern_.Source(bitmap);
  for (std::size_t i = 0; i < dots_.size(); ++i) {
    auto& dot = dots_[i];
    dot.Visibility(i < value_.point_count ? xaml::Visibility::Visible
                                          : xaml::Visibility::Collapsed);
    if (i >= value_.point_count)
      continue;
    colors_[i].Color(NativeColor(value_.points[i].color));
    controls::Canvas::SetLeft(
        dot, value_.points[i].position.x * side_ - dot.Width() / 2);
    controls::Canvas::SetTop(
        dot, value_.points[i].position.y * side_ - dot.Height() / 2);
  }
  hint_.Visibility(value_.point_count ? xaml::Visibility::Collapsed
                                      : xaml::Visibility::Visible);
}

void ThemeCanvas::Render(const WorkspaceTheme& value, float available_width) {
  value_ = value;
  // A scroll bar or layout rounding can make the arranged board narrower than
  // the requested width. Keep its measured size through live color updates.
  const auto measured_width = static_cast<float>(root_.ActualWidth());
  side_ = measured_width > 0 ? measured_width : std::max(0.0f, available_width);
  if (root_.Height() != side_)
    root_.Height(side_);
  Arrange();
}

void ThemeCanvas::SetText(UiTextView text) {
  Accessible(root_, NativeText(UiText::kThemeCanvas, text));
  const auto hint = NativeText(UiText::kThemeCanvasHint, text);
  if (hint_.Text() != hint)
    hint_.Text(hint);
}
}  // namespace kr::win
