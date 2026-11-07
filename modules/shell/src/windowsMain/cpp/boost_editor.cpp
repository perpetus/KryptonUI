#include "boost_editor.hpp"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.System.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

#include "krypton/ui/platform/platform.hpp"
#include "krypton/ui/presentation/boost.hpp"
#include "native_input.hpp"

namespace kr::win {
namespace {
winrt::Windows::UI::Color Native(Color c) {
  return {c.a(), c.r(), c.g(), c.b()};
}
winrt::Windows::UI::Color Native(std::uint32_t c) {
  return Native(Color{c});
}
Color Shared(winrt::Windows::UI::Color c) {
  return Color::FromArgb(c.A, c.R, c.G, c.B);
}
void Tint(const xaml::UIElement& element, const media::Brush& brush) {
  if (const auto text = element.try_as<controls::TextBlock>())
    text.Foreground(brush);
  else if (const auto box = element.try_as<controls::Viewbox>()) {
    const auto canvas = box.Child().try_as<controls::Canvas>();
    if (canvas && canvas.Children().Size())
      if (const auto path =
              canvas.Children().GetAt(0).try_as<xaml::Shapes::Path>()) {
        if (path.StrokeThickness() > 0)
          path.Stroke(brush);
        else
          path.Fill(brush);
      }
  }
}
}  // namespace
BoostEditor::BoostEditor(HWND parent, ViewContext& context)
    : context_(context), island_(parent) {}
BoostEditor::~BoostEditor() {
  CancelHeaderDrag();
  CancelPaletteDrag();
  try {
    if (palette_ && palette_pressed_)
      palette_.RemoveHandler(xaml::UIElement::PointerPressedEvent(),
                             winrt::box_value(palette_pressed_));
    if (header_ && header_pressed_)
      header_.RemoveHandler(xaml::UIElement::PointerPressedEvent(),
                            winrt::box_value(header_pressed_));
  } catch (...) {
  }
  lifetime_.reset();
  Close();
}
controls::Button BoostEditor::Action(std::wstring_view label,
                                     const design::icons::Icon* icon,
                                     std::move_only_function<void()> action) {
  const auto& t = kTokens.boost_editor;
  auto native = IconButton(context_.brushes, L"", label, std::move(action));
  auto button = native->control;
  button.Width(std::numeric_limits<double>::quiet_NaN());
  button.Height(t.button_height);
  button.MinWidth(0);
  button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  button.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
  button.Padding({t.padding, 0, t.padding, 0});
  button.Margin(UniformThickness(0));
  button.CornerRadius(UniformCorners(t.radius));
  controls::Grid content;
  AddColumn(content, 1, xaml::GridUnitType::Star);
  AddColumn(content, 0, xaml::GridUnitType::Auto);
  auto text = Label(label, t.font_size);
  text.VerticalAlignment(xaml::VerticalAlignment::Center);
  content.Children().Append(text);
  if (icon) {
    auto visual = SourceIcon(context_.brushes.text, *icon);
    visual.Width(t.icon_size);
    visual.Height(t.icon_size);
    visual.VerticalAlignment(xaml::VerticalAlignment::Center);
    controls::Grid::SetColumn(visual, 1);
    if (label.empty()) {
      text.Visibility(xaml::Visibility::Collapsed);
      controls::Grid::SetColumn(visual, 0);
      controls::Grid::SetColumnSpan(visual, 2);
    }
    content.Children().Append(visual);
  }
  button.Content(content);
  buttons_.push_back(std::move(native));
  return button;
}
void BoostEditor::Build() {
  if (popup_)
    return;
  const auto& t = kTokens.boost_editor;
  island_.Ensure();
  context_.brushes.Install(island_.Root());
  popup_ = controls::Primitives::Popup();
  popup_.IsLightDismissEnabled(true);
  popup_.ShouldConstrainToRootBounds(true);
  surface_ = controls::Border();
  surface_.Width(t.width);
  surface_.CornerRadius(UniformCorners(t.radius));
  surface_.BorderThickness(UniformThickness(t.border_width));
  root_ = controls::Grid();
  AddRow(root_, t.header_height, xaml::GridUnitType::Pixel);
  AddRow(root_, 1, xaml::GridUnitType::Star);
  AddRow(root_, t.footer_height, xaml::GridUnitType::Pixel);
  surface_.Child(root_);
  header_ = controls::Grid();
  auto header = header_;
  AddColumn(header, 0, xaml::GridUnitType::Auto);
  AddColumn(header, 1, xaml::GridUnitType::Star);
  AddColumn(header, 0, xaml::GridUnitType::Auto);
  auto copy = Action(L"", &design::icons::kBoostDice, [this] {
    const auto ticks = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    RandomizeBoost(draft_, static_cast<std::uint32_t>(ticks ^ (ticks >> 32)));
    (void)Send(BoostEdit::kPreview);
  });
  shuffle_ = copy;
  Accessible(copy, winrt::to_hstring(boost_text::kRandomizeBoost));
  copy.Padding(UniformThickness(0));
  copy.Width(t.header_button_size);
  header.Children().Append(copy);
  title_button_ = Action(winrt::to_hstring(boost_text::kMyBoost),
                         &design::icons::kBoostChevron, [this] { Details(); });
  title_button_.Padding(UniformThickness(t.font_panel_padding));
  title_button_.IsHitTestVisible(false);
  title_ = title_button_.Content()
               .as<controls::Grid>()
               .Children()
               .GetAt(0)
               .as<controls::TextBlock>();
  title_.FontSize(t.header_size);
  title_.FontWeight(kMedium);
  title_.TextAlignment(xaml::TextAlignment::Center);
  title_.Margin({t.icon_size, 0, 0, 0});
  title_.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
  controls::Grid::SetColumn(title_button_, 1);
  header.Children().Append(title_button_);
  close_ = Action(L"", &design::icons::kBoostClose,
                  [this] { (void)Send(BoostEdit::kCommit); });
  Accessible(close_, winrt::to_hstring(boost_text::kCloseBoostEditor));
  close_.Padding(UniformThickness(0));
  close_.Width(t.header_button_size);
  controls::Grid::SetColumn(close_, 2);
  header.Children().Append(close_);
  for (auto button : {copy, close_}) {
    button.Height(t.header_button_size);
    button.VerticalAlignment(xaml::VerticalAlignment::Center);
  }
  title_button_.Height(t.header_height);
  BuildHeaderInput();
  root_.Children().Append(header);
  controls::StackPanel body;
  body.Spacing(t.gap);
  body.Padding({t.padding, t.body_padding_top, t.padding, t.padding});
  palette_ = controls::Border();
  palette_.Width(t.palette_size);
  palette_.Height(t.palette_size);
  palette_.Padding(UniformThickness(t.palette_padding));
  palette_.CornerRadius(UniformCorners(t.palette_radius));
  palette_frame_ = media::ImageBrush();
  palette_frame_.Stretch(media::Stretch::Fill);
  palette_frame_.ImageSource(palette_pattern_.HueWheel(
      static_cast<int>(t.palette_size), static_cast<int>(t.palette_size)));
  palette_.Background(palette_frame_);
  controls::Border inner;
  inner.CornerRadius(UniformCorners(t.radius));
  palette_canvas_ = controls::Canvas();
  inner.Child(palette_canvas_);
  palette_.Child(inner);
  dots_ = Label(L"", t.dot_pitch);
  dots_.FontFamily(media::FontFamily(L"Consolas"));
  dots_.LineHeight(t.dot_pitch);
  dots_.IsHitTestVisible(false);
  std::wstring dots;
  const auto count =
      static_cast<int>((t.palette_size - 2 * t.palette_padding) / t.dot_pitch);
  for (int y = 0; y < count; ++y) {
    for (int x = 0; x < count; ++x)
      dots += L"\u00b7 ";
    dots += L"\n";
  }
  dots_.Text(dots);
  palette_canvas_.Children().Append(dots_);
  orbit_ = xaml::Shapes::Ellipse();
  const auto orbit = orbit_;
  orbit.Width(t.orbit_size);
  orbit.Height(t.orbit_size);
  orbit.StrokeThickness(t.border_width);
  orbit.Stroke(context_.brushes.search_border);
  orbit.IsHitTestVisible(false);
  const auto center =
      (t.palette_size - 2 * t.palette_padding - t.orbit_size) / 2;
  controls::Canvas::SetLeft(orbit, center);
  controls::Canvas::SetTop(orbit, center);
  palette_canvas_.Children().Append(orbit);
  accent_ = Action(L"", &design::icons::kBoostMagic, [this] {
    draft_.auto_theme = !draft_.auto_theme;
    draft_.appearance = 0;
    draft_.color_enabled = true;
    if (draft_.auto_theme)
      draft_.accent = workspace_accent_;
    (void)Send(BoostEdit::kPreview);
  });
  const auto star = accent_;
  Accessible(star, winrt::to_hstring(boost_text::kWorkspaceColors));
  star.Width(t.foreground_knob);
  star.Height(t.foreground_knob);
  star.Padding(UniformThickness(0));
  controls::Canvas::SetLeft(
      star, (t.palette_size - 2 * t.palette_padding - t.foreground_knob) / 2);
  palette_canvas_.Children().Append(star);
  background_ = Action(L"", nullptr, [this] { Colors(true); });
  foreground_ = Action(L"", nullptr, [this] { Colors(false); });
  Accessible(background_, winrt::to_hstring(boost_text::kPageBackgroundColor));
  Accessible(foreground_, winrt::to_hstring(boost_text::kTextColor));
  for (auto button : {background_, foreground_}) {
    // The palette owns mouse capture; ButtonBase must not capture it first.
    // The buttons retain keyboard focus and their native Invoke action.
    button.IsHitTestVisible(false);
    button.Padding(UniformThickness(0));
    button.BorderThickness(UniformThickness(t.knob_border));
    button.BorderBrush(context_.brushes.search_text);
    button.CornerRadius(UniformCorners(t.background_knob));
    palette_canvas_.Children().Append(button);
  }
  background_.Width(t.background_knob);
  background_.Height(t.background_knob);
  foreground_.Width(t.foreground_knob);
  foreground_.Height(t.foreground_knob);
  controls::Canvas::SetLeft(background_, t.orbit_size - t.background_knob / 2);
  controls::Canvas::SetTop(background_, t.orbit_size - t.background_knob);
  controls::Canvas::SetLeft(
      foreground_,
      (t.palette_size - 2 * t.palette_padding - t.foreground_knob) / 2);
  controls::Canvas::SetTop(foreground_, t.orbit_size - t.foreground_knob / 2);
  BuildPaletteInput();
  body.Children().Append(palette_);
  controls::Grid modes;
  modes.ColumnSpacing(t.gap / 2);
  for (int i = 0; i < 3; ++i)
    AddColumn(modes, 1, xaml::GridUnitType::Star);
  appearance_ = Action(L"", &design::icons::kBoostAppearance, [this] {
    draft_.invert_colors = !draft_.invert_colors;
    (void)Send(BoostEdit::kPreview);
  });
  adjust_ =
      Action(L"", &design::icons::kBoostControls, [this] { ColorSettings(); });
  enabled_ = Action(L"", &design::icons::kBoostDisable, [this] {
    draft_.color_enabled = !draft_.color_enabled;
    (void)Send(BoostEdit::kPreview);
  });
  Accessible(appearance_, winrt::to_hstring(boost_text::kPageAppearance));
  Accessible(adjust_, winrt::to_hstring(boost_text::kTextAndAccentColors));
  Accessible(enabled_, winrt::to_hstring(boost_text::kEnableOrDisableBoost));
  int column = 0;
  for (auto button : {appearance_, adjust_, enabled_}) {
    button.Padding(UniformThickness(t.padding / 2));
    controls::Grid::SetColumn(button, column++);
    modes.Children().Append(button);
  }
  body.Children().Append(modes);
  fonts_ = controls::Grid();
  for (int i = 0; i < t.font_columns; ++i)
    AddColumn(fonts_, 1, xaml::GridUnitType::Star);
  for (std::size_t i = 0; i < kBoostFonts.size(); ++i) {
    if (i % static_cast<std::size_t>(t.font_columns) == 0)
      AddRow(fonts_, t.font_cell_height, xaml::GridUnitType::Pixel);
    auto button =
        Action(winrt::to_hstring(boost_text::kAa), nullptr, [this, i] {
          draft_.font = static_cast<std::uint32_t>(i);
          (void)Send(BoostEdit::kPreview);
        });
    button.Height(t.font_cell_height);
    button.Padding(UniformThickness(0));
    button.HorizontalContentAlignment(xaml::HorizontalAlignment::Center);
    auto sample = button.Content()
                      .as<controls::Grid>()
                      .Children()
                      .GetAt(0)
                      .as<controls::TextBlock>();
    if (i)
      sample.FontFamily(media::FontFamily(winrt::to_hstring(kBoostFonts[i])));
    Accessible(button,
               i ? winrt::to_hstring(kBoostFonts[i])
                 : winrt::hstring(winrt::to_hstring(boost_text::kSiteFont)));
    controls::Grid::SetRow(
        button, static_cast<int>(i / static_cast<std::size_t>(t.font_columns)));
    controls::Grid::SetColumn(
        button, static_cast<int>(i % static_cast<std::size_t>(t.font_columns)));
    fonts_.Children().Append(button);
    font_buttons_.push_back(button);
  }
  font_surface_ = controls::Border();
  auto font_surface = font_surface_;
  font_surface.CornerRadius(UniformCorners(t.radius));
  font_surface.Padding(UniformThickness(t.font_panel_padding));
  font_surface.Child(fonts_);
  body.Children().Append(font_surface);
  controls::Grid typography;
  typography.ColumnSpacing(t.gap / 2);
  AddColumn(typography, 1, xaml::GridUnitType::Star);
  AddColumn(typography, 1, xaml::GridUnitType::Star);
  size_ = Action(winrt::to_hstring(boost_text::kSize),
                 &design::icons::kBoostSize, [this] { Size(); });
  case_ = Action(winrt::to_hstring(boost_text::kCase),
                 &design::icons::kBoostCase, [this] { Case(); });
  size_.Padding(UniformThickness(t.font_panel_padding));
  case_.Padding(UniformThickness(t.font_panel_padding));
  controls::Grid::SetColumn(case_, 1);
  typography.Children().Append(size_);
  typography.Children().Append(case_);
  body.Children().Append(typography);
  body.Children().Append(Action(winrt::to_hstring(boost_text::kZap),
                                &design::icons::kBoostZap,
                                [this] { (void)Send(BoostEdit::kZap); }));
  body.Children().Append(Action(winrt::to_hstring(boost_text::kFocus),
                                &design::icons::kBoostFocus,
                                [this] { (void)Send(BoostEdit::kFocus); }));
  code_ = Action(winrt::to_hstring(boost_text::kCode),
                 &design::icons::kBoostCode, [this] { Code(); });
  body.Children().Append(code_);
  controls::ScrollViewer scroll;
  scroll.HorizontalScrollBarVisibility(controls::ScrollBarVisibility::Disabled);
  scroll.VerticalScrollBarVisibility(controls::ScrollBarVisibility::Auto);
  scroll.Content(body);
  controls::Grid::SetRow(scroll, 1);
  root_.Children().Append(scroll);
  footer_ = controls::Border();
  footer_.Padding(UniformThickness(t.padding));
  auto share =
      Action(winrt::to_hstring(boost_text::kShare), &design::icons::kBoostShare,
             [this] { (void)Send(BoostEdit::kShare); });
  footer_.Child(share);
  controls::Grid::SetRow(footer_, 2);
  root_.Children().Append(footer_);
  fixed_buttons_ = buttons_.size();
  popup_.Child(surface_);
  island_.Root().Children().Append(popup_);
  loaded_ = island_.Root().Loaded(winrt::auto_revoke,
                                  [this](auto&&, auto&&) { Open(); });
  palette_sized_ =
      island_.Root().SizeChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
        CancelPaletteDrag();
        CancelHeaderDrag();
      });
  fallback_ = surface_.ProcessKeyboardAccelerators(
      winrt::auto_revoke,
      [this](auto&&,
             const xaml::Input::ProcessKeyboardAcceleratorEventArgs& args) {
        if (!args.Handled() && context_.input.ForwardUnhandledKey(
                                   static_cast<std::uint32_t>(args.Key())))
          args.Handled(true);
      });
  key_ = surface_.KeyDown(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::KeyRoutedEventArgs& args) {
        if (!args.Handled() &&
            args.Key() == winrt::Windows::System::VirtualKey::Escape) {
          (void)Send(BoostEdit::kCommit);
          args.Handled(true);
        }
      });
}
void BoostEditor::BuildHeaderInput() {
  header_pressed_ = [this, weak = std::weak_ptr<int>(lifetime_)](
                        auto&&,
                        const xaml::Input::PointerRoutedEventArgs& args) {
    if (weak.expired() || !requested_ || header_pointer_ || palette_pointer_ ||
        args.IsGenerated() ||
        !args.GetCurrentPoint(header_).Properties().IsLeftButtonPressed())
      return;
    for (auto node = args.OriginalSource().try_as<xaml::DependencyObject>();
         node; node = media::VisualTreeHelper::GetParent(node))
      if (node == close_ || node == shuffle_)
        return;
    if (!header_.CapturePointer(args.Pointer()))
      return;
    header_pointer_ = args.Pointer().PointerId();
    header_start_ = header_last_ = args.GetCurrentPoint(nullptr).Position();
    header_origin_ = {panel_bounds_.x, panel_bounds_.y};
    header_dragged_ = false;
    const auto title = args.GetCurrentPoint(title_button_).Position();
    header_title_hit_ = title.X >= 0 && title.Y >= 0 &&
                        title.X < title_button_.ActualWidth() &&
                        title.Y < title_button_.ActualHeight();
    args.Handled(true);
  };
  header_.AddHandler(xaml::UIElement::PointerPressedEvent(),
                     winrt::box_value(header_pressed_), true);
  header_moved_ = header_.PointerMoved(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (header_pointer_ != args.Pointer().PointerId())
          return;
        args.Handled(true);
        if (!args.GetCurrentPoint(header_).Properties().IsLeftButtonPressed())
          CancelHeaderDrag();
        else
          MoveHeader(args);
      });
  header_released_ = header_.PointerReleased(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (header_pointer_ != args.Pointer().PointerId())
          return;
        MoveHeader(args);
        const bool click = !header_dragged_ && header_title_hit_;
        args.Handled(true);
        CancelHeaderDrag();
        if (click)
          Details();
      });
  header_lost_ = header_.PointerCaptureLost(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (header_pointer_ == args.Pointer().PointerId() &&
            args.OriginalSource() == header_)
          header_pointer_.reset();
      });
  header_cancelled_ = header_.PointerCanceled(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (header_pointer_ == args.Pointer().PointerId())
          CancelHeaderDrag();
      });
}
void BoostEditor::MoveHeader(const xaml::Input::PointerRoutedEventArgs& args) {
  if (args.IsGenerated())
    return;
  const auto p = args.GetCurrentPoint(nullptr).Position();
  if (p == header_last_)
    return;
  header_last_ = p;
  if (!header_dragged_ &&
      std::hypot(p.X - header_start_.X, p.Y - header_start_.Y) <
          kTokens.boost_editor.drag_threshold)
    return;
  header_dragged_ = true;
  panel_position_ = Point{header_origin_.x + p.X - header_start_.X,
                          header_origin_.y + p.Y - header_start_.Y};
  ApplyPlacement();
}
void BoostEditor::CancelHeaderDrag() noexcept {
  const bool captured = header_pointer_.has_value();
  header_pointer_.reset();
  header_dragged_ = false;
  try {
    if (captured && header_)
      header_.ReleasePointerCaptures();
  } catch (...) {
  }
}
void BoostEditor::ApplyPlacement() {
  panel_bounds_ = PlaceBoostEditor(panel_client_, panel_position_);
  surface_.Width(panel_bounds_.width);
  surface_.Height(panel_bounds_.height);
  popup_.HorizontalOffset(panel_bounds_.x - panel_client_.x);
  popup_.VerticalOffset(panel_bounds_.y - panel_client_.y);
  context_.glass.Place(surface_, panel_bounds_);
}

void BoostEditor::BuildPaletteInput() {
  palette_pressed_ = [this, weak = std::weak_ptr<int>(lifetime_)](
                         auto&&,
                         const xaml::Input::PointerRoutedEventArgs& args) {
    if (weak.expired() || palette_pointer_ || args.IsGenerated() ||
        !requested_ ||
        !args.GetCurrentPoint(palette_canvas_)
             .Properties()
             .IsLeftButtonPressed())
      return;
    for (auto node = args.OriginalSource().try_as<xaml::DependencyObject>();
         node; node = media::VisualTreeHelper::GetParent(node))
      if (node == accent_)
        return;
    const auto p = args.GetCurrentPoint(palette_canvas_).Position();
    const float side = kTokens.boost_editor.palette_size -
                       2 * kTokens.boost_editor.palette_padding;
    const Point normalized{p.X / side, p.Y / side};
    const auto hit = HitBoostPoint(palette_state_, normalized);
    if (!palette_.CapturePointer(args.Pointer()))
      return;
    palette_pointer_ = args.Pointer().PointerId();
    palette_start_ = palette_last_ = args.GetCurrentPoint(nullptr).Position();
    palette_dragged_ = false;
    palette_hit_ = hit.has_value();
    palette_target_ = hit.value_or(BoostColorPoint::kBackground);
    palette_offset_ = {};
    if (hit) {
      const auto points = BoostPoints(palette_state_);
      const auto center = *hit == BoostColorPoint::kBackground
                              ? points.background
                              : points.foreground;
      palette_offset_ = {center.x - normalized.x, center.y - normalized.y};
    }
    args.Handled(true);
  };
  palette_.AddHandler(xaml::UIElement::PointerPressedEvent(),
                      winrt::box_value(palette_pressed_), true);
  palette_moved_ = palette_.PointerMoved(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (palette_pointer_ != args.Pointer().PointerId())
          return;
        args.Handled(true);
        if (!args.GetCurrentPoint(palette_canvas_)
                 .Properties()
                 .IsLeftButtonPressed())
          CancelPaletteDrag();
        else
          MovePalette(args);
      });
  palette_released_ = palette_.PointerReleased(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (palette_pointer_ != args.Pointer().PointerId())
          return;
        MovePalette(args);
        const bool click = !palette_dragged_;
        const bool hit = palette_hit_;
        const auto target = palette_target_;
        const auto offset = palette_offset_;
        const auto p = args.GetCurrentPoint(palette_canvas_).Position();
        args.Handled(true);
        CancelPaletteDrag();
        if (click) {
          const float side = kTokens.boost_editor.palette_size -
                             2 * kTokens.boost_editor.palette_padding;
          if (MoveBoostPoint(palette_state_, draft_,
                             hit ? target : BoostColorPoint::kBackground,
                             {p.X / side + (hit ? offset.x : 0),
                              p.Y / side + (hit ? offset.y : 0)})) {
            palette_background_ = draft_.background;
            palette_foreground_ = draft_.foreground;
            (void)Send(BoostEdit::kPreview);
          }
          ArrangePalette();
        }
      });
  palette_lost_ = palette_.PointerCaptureLost(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (palette_pointer_ == args.Pointer().PointerId() &&
            args.OriginalSource() == palette_)
          palette_pointer_.reset();
      });
  palette_cancelled_ = palette_.PointerCanceled(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (palette_pointer_ == args.Pointer().PointerId())
          CancelPaletteDrag();
      });
}
void BoostEditor::MovePalette(const xaml::Input::PointerRoutedEventArgs& args) {
  if (args.IsGenerated())
    return;
  const auto point = args.GetCurrentPoint(nullptr).Position();
  if (point == palette_last_)
    return;
  palette_last_ = point;
  if (!palette_dragged_ &&
      std::hypot(point.X - palette_start_.X, point.Y - palette_start_.Y) <
          kTokens.boost_editor.drag_threshold)
    return;
  palette_dragged_ = true;
  const auto p = args.GetCurrentPoint(palette_canvas_).Position();
  const float side = kTokens.boost_editor.palette_size -
                     2 * kTokens.boost_editor.palette_padding;
  const bool changed = MoveBoostPoint(
      palette_state_, draft_, palette_target_,
      {p.X / side + palette_offset_.x, p.Y / side + palette_offset_.y});
  palette_background_ = draft_.background;
  palette_foreground_ = draft_.foreground;
  ArrangePalette();
  if (changed)
    (void)Send(BoostEdit::kPreview);
}
void BoostEditor::CancelPaletteDrag() noexcept {
  const bool captured = palette_pointer_.has_value();
  palette_pointer_.reset();
  palette_dragged_ = false;
  try {
    if (captured && palette_)
      palette_.ReleasePointerCaptures();
  } catch (...) {
  }
}
void BoostEditor::ArrangePalette() {
  const auto points = BoostPoints(palette_state_);
  const auto& t = kTokens.boost_editor;
  const float side = t.palette_size - 2 * t.palette_padding;
  controls::Canvas::SetLeft(background_,
                            points.background.x * side - t.background_knob / 2);
  controls::Canvas::SetTop(background_,
                           points.background.y * side - t.background_knob / 2);
  controls::Canvas::SetLeft(foreground_,
                            points.foreground.x * side - t.foreground_knob / 2);
  controls::Canvas::SetTop(foreground_,
                           points.foreground.y * side - t.foreground_knob / 2);
  const float size = t.orbit_size * palette_state_.saturation;
  orbit_.Width(size);
  orbit_.Height(size);
  controls::Canvas::SetLeft(orbit_, (side - size) / 2);
  controls::Canvas::SetTop(orbit_, (side - size) / 2);
}

void BoostEditor::ShowFlyout(const controls::Button& target,
                             const xaml::UIElement& content) {
  if (flyout_)
    flyout_.Hide();
  flyout_ = controls::Flyout();
  flyout_.Content(content);
  flyout_.Placement(
      controls::Primitives::FlyoutPlacementMode::LeftEdgeAlignedTop);
  const auto generation = ++flyout_generation_;
  const auto weak = std::weak_ptr<int>(lifetime_);
  flyout_opened_ = flyout_.Opened(
      winrt::auto_revoke, [this, weak, generation](auto&&, auto&&) {
        if (!weak.expired() && generation == flyout_generation_)
          flyout_open_ = true;
      });
  flyout_closed_ = flyout_.Closed(
      winrt::auto_revoke, [this, weak, generation](auto&&, auto&&) {
        if (weak.expired() || generation != flyout_generation_)
          return;
        flyout_open_ = false;
        if (closing_)
          QueueHide(generation_);
      });
  flyout_.ShowAt(target);
}
void BoostEditor::PrepareFlyout() {
  CancelHeaderDrag();
  CancelPaletteDrag();
  if (flyout_)
    flyout_.Hide();
  color_changed_.revoke();
  code_changed_.revoke();
  size_changed_.revoke();
  for (auto& changed : color_settings_changed_)
    changed.revoke();
  buttons_.resize(fixed_buttons_);
}
void BoostEditor::Colors(int role) {
  PrepareFlyout();
  color_ = controls::ColorPicker();
  color_.IsAlphaEnabled(false);
  const auto current = role == 1   ? draft_.background
                       : role == 2 ? draft_.accent
                                   : draft_.foreground;
  const auto& colors =
      dark_ ? kTokens.boost_editor.dark : kTokens.boost_editor.light;
  color_.Color(Native(current.argb ? current
                      : role == 1  ? Color{colors.default_background}
                                   : Color{colors.default_foreground}));
  color_changed_ = color_.ColorChanged(
      winrt::auto_revoke,
      [this, role](auto&&, const controls::ColorChangedEventArgs& args) {
        if (role == 1)
          draft_.background = Shared(args.NewColor());
        else if (role == 2)
          draft_.accent = Shared(args.NewColor());
        else
          draft_.foreground = Shared(args.NewColor());
        draft_.palette_set = false;
        draft_.auto_theme = false;
        draft_.color_enabled = true;
        (void)Send(BoostEdit::kPreview);
      });
  controls::StackPanel content;
  content.Children().Append(color_);
  content.Children().Append(Action(winrt::to_hstring(boost_text::kUseSiteColor),
                                   nullptr, [this, role] {
                                     if (role == 1)
                                       draft_.background = {};
                                     else if (role == 2)
                                       draft_.accent = {};
                                     else
                                       draft_.foreground = {};
                                     (void)Send(BoostEdit::kPreview);
                                     if (flyout_)
                                       flyout_.Hide();
                                   }));
  ShowFlyout(role == 1   ? background_
             : role == 2 ? adjust_
                         : foreground_,
             content);
}
void BoostEditor::ColorSettings() {
  PrepareFlyout();
  controls::StackPanel content;
  content.Width(kTokens.boost_editor.color_controls_width);
  content.Spacing(kTokens.boost_editor.gap);
  const std::array labels{boost_text::kContrast, boost_text::kBrightness,
                          boost_text::kOriginalSaturation};
  const std::array values{draft_.contrast, draft_.brightness,
                          draft_.saturation};
  for (std::size_t i = 0; i < color_sliders_.size(); ++i) {
    auto slider = color_sliders_[i] = controls::Slider();
    slider.Header(winrt::box_value(winrt::to_hstring(labels[i])));
    slider.Minimum(i ? 0 : kTokens.boost_editor.contrast_min);
    slider.Maximum(i ? 1 : kTokens.boost_editor.contrast_max);
    slider.StepFrequency(kTokens.boost_editor.color_step);
    slider.Value(values[i]);
    Accessible(slider, winrt::to_hstring(labels[i]));
    color_settings_changed_[i] = slider.ValueChanged(
        winrt::auto_revoke,
        [this, i](
            auto&&,
            const controls::Primitives::RangeBaseValueChangedEventArgs& args) {
          if (changing_)
            return;
          const auto value = static_cast<float>(args.NewValue());
          if (i == 0)
            draft_.contrast = value;
          else if (i == 1)
            draft_.brightness = value;
          else
            draft_.saturation = value;
          draft_.color_enabled = true;
          draft_.appearance = 0;
          draft_.palette_hue = palette_state_.hue;
          draft_.palette_distance = palette_state_.saturation;
          draft_.secondary_rotation = palette_state_.secondary_delta;
          draft_.palette_set = true;
          (void)Send(BoostEdit::kPreview);
        });
    content.Children().Append(slider);
  }
  ShowFlyout(adjust_, content);
}

void BoostEditor::Details() {
  PrepareFlyout();
  controls::StackPanel content;
  content.Spacing(kTokens.boost_editor.gap / 2);
  name_ = controls::TextBox();
  name_.Header(winrt::box_value(winrt::to_hstring(boost_text::kName)));
  name_.Text(winrt::to_hstring(draft_.name));
  pattern_ = controls::TextBox();
  pattern_.Header(
      winrt::box_value(winrt::to_hstring(boost_text::kSiteOrUrlPattern)));
  pattern_.Text(winrt::to_hstring(draft_.pattern));
  content.Children().Append(name_);
  content.Children().Append(pattern_);
  match_ = controls::ComboBox();
  match_.Header(winrt::box_value(winrt::to_hstring(boost_text::kMatch)));
  for (auto label : {winrt::to_hstring(boost_text::kDomain),
                     winrt::to_hstring(boost_text::kUrlContains),
                     winrt::to_hstring(boost_text::kUrlEquals),
                     winrt::to_hstring(boost_text::kRegularExpression)})
    match_.Items().Append(winrt::box_value(label));
  match_.SelectedIndex(static_cast<int>(draft_.match));
  content.Children().Append(match_);
  auto error =
      Label(winrt::to_hstring(boost_text::kEnterANameAndAValidSiteOrUrlPattern),
            kTokens.boost_editor.label_size);
  error.TextWrapping(xaml::TextWrapping::Wrap);
  error.Visibility(xaml::Visibility::Collapsed);
  content.Children().Append(error);
  content.Children().Append(
      Action(winrt::to_hstring(boost_text::kSave), nullptr, [this, error] {
        const auto name = winrt::to_string(name_.Text());
        const auto pattern = winrt::to_string(pattern_.Text());
        if (name.empty() || pattern.empty()) {
          error.Visibility(xaml::Visibility::Visible);
          return;
        }
        draft_.name = name;
        draft_.pattern = pattern;
        draft_.match = static_cast<std::uint32_t>(match_.SelectedIndex());
        if (Send(BoostEdit::kPreview))
          flyout_.Hide();
        else
          error.Visibility(xaml::Visibility::Visible);
      }));
  content.Children().Append(Action(winrt::to_hstring(boost_text::kDeleteBoost),
                                   &design::icons::kBoostDelete,
                                   [this] { (void)Send(BoostEdit::kRemove); }));
  ShowFlyout(title_button_, content);
}
void BoostEditor::Code() {
  PrepareFlyout();
  code_changed_.revoke();
  code_text_ = controls::TextBox();
  code_text_.AcceptsReturn(true);
  code_text_.TextWrapping(xaml::TextWrapping::NoWrap);
  code_text_.Width(kTokens.boost_editor.code_width);
  code_text_.Height(kTokens.boost_editor.code_height);
  code_text_.FontFamily(media::FontFamily(L"Consolas"));
  code_text_.Text(winrt::to_hstring(draft_.css));
  Accessible(code_text_, winrt::to_hstring(boost_text::kCustomCss));
  code_changed_ =
      code_text_.TextChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
        if (!changing_) {
          draft_.css = winrt::to_string(code_text_.Text());
          (void)Send(BoostEdit::kPreview);
        }
      });
  ShowFlyout(code_, code_text_);
}
void BoostEditor::Size() {
  PrepareFlyout();
  size_changed_.revoke();
  size_slider_ = controls::Slider();
  size_slider_.Minimum(50);
  size_slider_.Maximum(200);
  size_slider_.StepFrequency(5);
  size_slider_.Value(draft_.size);
  size_slider_.Width(kTokens.boost_editor.palette_size);
  Accessible(size_slider_, winrt::to_hstring(boost_text::kTextSizePercent));
  size_changed_ = size_slider_.ValueChanged(
      winrt::auto_revoke,
      [this](auto&&,
             const controls::Primitives::RangeBaseValueChangedEventArgs& args) {
        draft_.size = static_cast<std::uint32_t>(args.NewValue());
        (void)Send(BoostEdit::kPreview);
      });
  ShowFlyout(size_, size_slider_);
}
void BoostEditor::Case() {
  PrepareFlyout();
  controls::StackPanel content;
  const std::array labels{winrt::to_hstring(boost_text::kOriginal),
                          winrt::to_hstring(boost_text::kUppercase),
                          winrt::to_hstring(boost_text::kLowercase),
                          winrt::to_hstring(boost_text::kTitleCase)};
  for (std::size_t i = 0; i < labels.size(); ++i)
    content.Children().Append(Action(labels[i], nullptr, [this, i] {
      draft_.letter_case = static_cast<std::uint32_t>(i);
      (void)Send(BoostEdit::kPreview);
      flyout_.Hide();
    }));
  ShowFlyout(case_, content);
}
void BoostEditor::Appearance() {
  PrepareFlyout();
  controls::StackPanel content;
  const std::array labels{winrt::to_hstring(boost_text::kUseSiteAppearance),
                          winrt::to_hstring(boost_text::kLight),
                          winrt::to_hstring(boost_text::kDark)};
  for (std::size_t i = 0; i < labels.size(); ++i)
    content.Children().Append(Action(labels[i], nullptr, [this, i] {
      draft_.appearance = static_cast<std::uint32_t>(i);
      (void)Send(BoostEdit::kPreview);
      flyout_.Hide();
    }));
  ShowFlyout(appearance_, content);
}
bool BoostEditor::Send(BoostEdit kind) {
  if (!requested_ || changing_)
    return false;
  if (kind == BoostEdit::kPreview)
    draft_.enabled = true;
  const bool pick = kind == BoostEdit::kZap || kind == BoostEdit::kFocus;
  if (pick)
    Close();
  if (!context_.delegate.OnBoostEdited(kind, draft_)) {
    if (kind == BoostEdit::kCommit) {
      (void)context_.delegate.OnBoostEdited(BoostEdit::kCancel, draft_);
      Close();
    }
    return false;
  }
  if (kind == BoostEdit::kCommit || kind == BoostEdit::kCancel ||
      kind == BoostEdit::kRemove || kind == BoostEdit::kZap ||
      kind == BoostEdit::kFocus)
    Close();
  else
    Refresh();
  return true;
}
void BoostEditor::Refresh() {
  const auto& t = kTokens.boost_editor;
  const auto& colors = dark_ ? t.dark : t.light;
  if (!text_) {
    text_ = media::SolidColorBrush();
    tile_ = media::SolidColorBrush();
    muted_ = media::SolidColorBrush();
    foreground_fill_ = media::SolidColorBrush();
    background_fill_ = media::SolidColorBrush();
    surface_fill_ = media::SolidColorBrush();
    header_fill_ = media::SolidColorBrush();
    dots_fill_ = media::SolidColorBrush();
    palette_fill_ = media::SolidColorBrush();
    accent_fill_ = media::SolidColorBrush();
    accent_text_ = media::SolidColorBrush();
  }
  surface_fill_.Color(Native(colors.background));
  header_fill_.Color(Native(colors.header));
  dots_fill_.Color(Native(colors.dots));
  palette_fill_.Color(Native(colors.tile));
  accent_fill_.Color(Native(colors.accent));
  accent_text_.Color(Native(colors.accent_text));
  text_.Color(Native(colors.text));
  tile_.Color(Native(colors.tile));
  muted_.Color(Native(colors.muted));
  surface_.Background(
      context_.glass.For(surface_, GlassRole::kMenu, surface_fill_, t.radius));
  surface_.BorderBrush(context_.brushes.menu_border);
  footer_.Background(header_fill_);
  header_.Background(header_fill_);
  font_surface_.Background(tile_);
  dots_.Foreground(dots_fill_);
  palette_.Child().as<controls::Border>().Background(palette_fill_);
  foreground_fill_.Color(Native(draft_.foreground.argb
                                    ? draft_.foreground
                                    : Color{colors.default_foreground}));
  background_fill_.Color(Native(draft_.background.argb
                                    ? draft_.background
                                    : Color{colors.default_background}));
  for (auto& native : buttons_) {
    native->control.Background(tile_);
    native->control.Foreground(text_);
    if (const auto grid = native->control.Content().try_as<controls::Grid>())
      for (const auto& child : grid.Children())
        Tint(child, text_);
  }
  title_.Text(winrt::to_hstring(draft_.name));
  background_.Background(background_fill_);
  foreground_.Background(foreground_fill_);
  enabled_.Opacity(draft_.color_enabled ? 1 : .45);
  appearance_.Opacity(draft_.invert_colors ? 1 : .45);
  accent_.Opacity(draft_.auto_theme ? 1 : .45);
  const media::Brush frame = !draft_.color_enabled || draft_.auto_theme
                                 ? media::Brush(header_fill_)
                                 : media::Brush(palette_frame_);
  if (palette_.Background() != frame)
    palette_.Background(frame);
  for (std::size_t i = 0; i < font_buttons_.size(); ++i)
    font_buttons_[i].BorderThickness(
        UniformThickness(i == draft_.font ? t.border_width : 0));
  const auto share = footer_.Child().as<controls::Button>();
  share.Background(accent_fill_);
  for (const auto& child : share.Content().as<controls::Grid>().Children())
    Tint(child, accent_text_);
  if (!palette_synced_ || palette_background_ != draft_.background ||
      palette_foreground_ != draft_.foreground) {
    palette_state_ = MakeBoostPalette(draft_);
    palette_background_ = draft_.background;
    palette_foreground_ = draft_.foreground;
    palette_synced_ = true;
  }
  ArrangePalette();
}
void BoostEditor::Open() {
  if (!requested_ || !island_.IsReady())
    return;
  popup_.IsOpen(true);
  if (focus_pending_ && close_.IsLoaded())
    focus_pending_ = !close_.Focus(xaml::FocusState::Programmatic);
}
void BoostEditor::Render(const BoostEditorState& state,
                         Rect client,
                         float scale,
                         xaml::ElementTheme theme,
                         Color workspace_accent) {
  workspace_accent_ = workspace_accent;
  if (!state.open || state.picking) {
    Close();
    return;
  }
  Build();
  const bool new_editor = draft_.serial != state.draft.serial;
  if (!requested_ || draft_.serial != state.draft.serial) {
    Close();
    if (new_editor)
      panel_position_.reset();
    palette_synced_ = false;
    ++generation_;
    focus_pending_ = true;
    const auto generation = generation_;
    const auto weak = std::weak_ptr<int>(lifetime_);
    closed_ = popup_.Closed(
        winrt::auto_revoke, [this, weak, generation](auto&&, auto&&) {
          if (!weak.expired() && generation == generation_ && requested_ &&
              !popup_.IsOpen())
            (void)Send(BoostEdit::kCommit);
        });
  }
  draft_ = state.draft;
  closing_ = false;
  requested_ = true;
  dark_ = theme == xaml::ElementTheme::Dark;
  surface_.RequestedTheme(theme);
  panel_client_ = client;
  ApplyPlacement();
  Refresh();
  island_.Show(client, scale, theme);
  visible_ = true;
  Open();
  island_.Raise();
}
void BoostEditor::QueueHide(std::uint64_t generation) noexcept try {
  if (hide_queued_ == generation)
    return;
  hide_queued_ = generation;
  const auto weak = std::weak_ptr<int>(lifetime_);
  const auto queue = island_.Root().DispatcherQueue();
  if (!queue.TryEnqueue([this, weak, generation] {
        if (weak.expired() || generation != generation_ || requested_)
          return;
        // Flyout focus restoration finishes before the island is hidden.
        island_.Hide();
        visible_ = false;
        closing_ = false;
        context_.hover_changed();
      })) {
    island_.Hide();
    visible_ = false;
    closing_ = false;
  }
} catch (...) {
  island_.Hide();
  visible_ = false;
  closing_ = false;
}
void BoostEditor::Close() noexcept {
  CancelHeaderDrag();
  CancelPaletteDrag();
  if (lifetime_ && ((!requested_ && !visible_) || closing_))
    return;
  requested_ = false;
  focus_pending_ = false;
  closing_ = true;
  const auto generation = ++generation_;
  closed_.revoke();
  try {
    if (flyout_)
      flyout_.Hide();
    if (popup_)
      popup_.IsOpen(false);
    if (surface_)
      context_.glass.Place(surface_, {});
    if (!lifetime_) {
      island_.Hide();
      visible_ = false;
      closing_ = false;
    } else if (!flyout_open_)
      QueueHide(generation);
  } catch (...) {
    island_.Hide();
    visible_ = false;
    closing_ = false;
  }
}
}  // namespace kr::win
