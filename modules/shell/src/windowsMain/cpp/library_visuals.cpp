#include "library_visuals.hpp"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>

#include <format>
#include <limits>
#include <numbers>
#include <utility>

namespace kr::win {
namespace {
constexpr auto& kStack = design::kTokens.download_stack;

shapes::Path LoadPath(const design::icons::Icon& icon) {
  const auto markup = std::format(
      LR"(<Path xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Data="{}" />)",
      icon.data);
  return xaml::Markup::XamlReader::Load(markup).as<shapes::Path>();
}
}  // namespace

bool IsWithin(const IInspectable& source, const xaml::UIElement& ancestor) {
  auto element = source.try_as<xaml::DependencyObject>();
  while (element) {
    if (element == ancestor)
      return true;
    element = media::VisualTreeHelper::GetParent(element);
  }
  return false;
}

std::wstring_view LibrarySectionGlyph(LibrarySection section) noexcept {
  switch (section) {
    case LibrarySection::kRouting:
      return library_glyphs::kRouting;
    case LibrarySection::kMedia:
      return library_glyphs::kMedia;
    case LibrarySection::kDownloads:
      return library_glyphs::kDownloads;
    case LibrarySection::kBoosts:
      return library_glyphs::kBoosts;
    case LibrarySection::kSpaces:
      return library_glyphs::kSpaces;
    case LibrarySection::kHistory:
      break;
  }
  return library_glyphs::kHistory;
}

SwitchIcon MakeSwitchIcon(const design::icons::Icon& first,
                          const design::icons::Icon* second,
                          double size) {
  SwitchIcon result;
  controls::Canvas canvas;
  canvas.Width(first.width);
  canvas.Height(first.height);
  result.first = LoadPath(first);
  canvas.Children().Append(result.first);
  if (second) {
    result.second = LoadPath(*second);
    result.second.Visibility(xaml::Visibility::Collapsed);
    canvas.Children().Append(result.second);
  }
  result.root = controls::Viewbox();
  result.root.Width(size);
  result.root.Height(size);
  result.root.Stretch(media::Stretch::Uniform);
  result.root.Child(canvas);
  return result;
}

void SwitchIcon::Show(bool second_shape) const {
  if (!second)
    return;
  const auto on = xaml::Visibility::Visible, off = xaml::Visibility::Collapsed;
  if (first.Visibility() != (second_shape ? off : on))
    first.Visibility(second_shape ? off : on);
  if (second.Visibility() != (second_shape ? on : off))
    second.Visibility(second_shape ? on : off);
}

void SwitchIcon::Fill(const media::Brush& brush) const {
  first.Fill(brush);
  if (second)
    second.Fill(brush);
}

DownloadBadge::DownloadBadge(ViewContext& context) : context_(context) {
  const auto picture_size = kStack.badge_size * kStack.badge_scale;
  root_.Width(kStack.badge_margin_start + kStack.badge_size +
              kStack.badge_margin_end);
  root_.VerticalAlignment(xaml::VerticalAlignment::Center);
  root_.IsHitTestVisible(false);

  picture_.Width(picture_size);
  picture_.Height(picture_size);
  picture_.CornerRadius(
      UniformCorners(kStack.badge_radius * kStack.badge_scale));
  picture_brush_.Stretch(media::Stretch::UniformToFill);
  picture_.Background(picture_brush_);
  placeholder_.Width(picture_size);
  placeholder_.Height(picture_size);
  placeholder_.CornerRadius(picture_.CornerRadius());
  placeholder_.Background(context_.brushes.pill);
  auto glyph = Glyph(library_glyphs::kFile, kTokens.type.size_large);
  glyph.Foreground(context_.brushes.icon);
  glyph.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  placeholder_.Child(glyph);

  // The pending badge: a filled 25px circle with a 3px transparent border,
  // a 31px ring around it and the down arrow inside.
  const auto ring = kStack.pending_size + 2 * kStack.pending_border;
  pending_.Width(ring);
  pending_.Height(ring);
  fill_.Width(kStack.pending_size - 2 * kStack.pending_border);
  fill_.Height(fill_.Width());
  for (auto* circle : {&track_, &arc_}) {
    const auto diameter = ring - kStack.ring_thickness;
    circle->Width(diameter);
    circle->Height(diameter);
    circle->StrokeThickness(kStack.ring_thickness);
  }
  arc_.RenderTransformOrigin({0.5, 0.5});
  media::RotateTransform rotate;
  rotate.Angle(-90);
  arc_.RenderTransform(rotate);
  arrow_ =
      std::make_unique<IconView>(context_.icons, IconRole::kArrowDown,
                                 fill_.Width() - 2 * 2, context_.brushes.icon);
  for (const xaml::UIElement& part :
       {fill_.as<xaml::UIElement>(), track_.as<xaml::UIElement>(),
        arc_.as<xaml::UIElement>(), arrow_->Root().as<xaml::UIElement>()})
    pending_.Children().Append(part);
  for (const auto& part : {picture_.as<xaml::FrameworkElement>(),
                           placeholder_.as<xaml::FrameworkElement>(),
                           pending_.as<xaml::FrameworkElement>()}) {
    part.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    part.VerticalAlignment(xaml::VerticalAlignment::Center);
    root_.Children().Append(part.as<xaml::UIElement>());
  }
  SetCompact(false);
}

void DownloadBadge::SetCompact(bool compact) {
  compact_ = compact;
  // Badges centre on Zen's 8px start margin plus half of the 20px badge; the
  // pending one sits 2px further left. The collapsed rail centres them.
  const double centre = kStack.badge_margin_start + kStack.badge_size / 2;
  const auto place = [&](const xaml::FrameworkElement& part, double shift) {
    part.HorizontalAlignment(compact ? xaml::HorizontalAlignment::Center
                                     : xaml::HorizontalAlignment::Left);
    part.Margin({compact ? 0 : centre - part.Width() / 2 + shift, 0, 0, 0});
  };
  place(picture_, 0);
  place(placeholder_, 0);
  place(pending_, -2);
  root_.Width(compact ? std::numeric_limits<double>::quiet_NaN()
                      : kStack.badge_margin_start + kStack.badge_size +
                            kStack.badge_margin_end);
  root_.HorizontalAlignment(compact ? xaml::HorizontalAlignment::Stretch
                                    : xaml::HorizontalAlignment::Left);
}

void DownloadBadge::Update(const DownloadVm& download,
                           const LibraryPaletteVm& palette,
                           const media::ImageSource& picture) {
  is_pending_ = download.pending;
  has_picture_ = static_cast<bool>(picture);
  const auto show = [](const xaml::UIElement& part, bool visible) {
    const auto value =
        visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
    if (part.Visibility() != value)
      part.Visibility(value);
  };
  show(pending_, is_pending_);
  show(picture_, !is_pending_ && has_picture_);
  show(placeholder_, !is_pending_ && !has_picture_);
  if (!is_pending_) {
    if (has_picture_ && picture_brush_.ImageSource() != picture)
      picture_brush_.ImageSource(picture);
    return;
  }
  const auto badge = context_.brushes.For(palette.badge);
  const auto track = context_.brushes.For(palette.badge_track);
  fill_.Fill(badge);
  track_.Stroke(track);
  arc_.Stroke(context_.brushes.For(palette.accent));
  arrow_->SetBrush(track);
  const float progress = download.progress_known ? download.progress : 0;
  if (progress == progress_)
    return;
  progress_ = progress;
  // Dash lengths count stroke widths.
  const double circumference =
      std::numbers::pi * arc_.Width() / kStack.ring_thickness;
  media::DoubleCollection dashes;
  dashes.Append(circumference * progress);
  dashes.Append(circumference);
  arc_.StrokeDashArray(dashes);
}

DownloadEntry::DownloadEntry(
    ViewContext& context,
    bool library_row,
    std::function<Rect(const xaml::FrameworkElement&)> bounds)
    : context_(context), bounds_(std::move(bounds)), badge_(context) {
  root_.Height(kStack.entry_height);
  root_.CornerRadius(UniformCorners(kStack.entry_radius));
  root_.Padding(
      {kStack.entry_padding_inline, 0, kStack.entry_padding_inline, 0});
  root_.Background(context_.brushes.transparent);
  AddColumn(root_, 0, xaml::GridUnitType::Auto);
  AddColumn(root_, 1, xaml::GridUnitType::Star);
  AddColumn(root_, 0, xaml::GridUnitType::Auto);
  root_.Children().Append(badge_.Root());

  title_ = Label(L"", kStack.title_size);
  title_.Foreground(context_.brushes.text);
  title_.Opacity(kStack.title_opacity);
  subtitle_ = Label(L"", kStack.subtitle_size);
  subtitle_.Foreground(context_.brushes.text);
  subtitle_.Opacity(kStack.subtitle_opacity);
  text_.VerticalAlignment(xaml::VerticalAlignment::Center);
  text_.Children().Append(title_);
  text_.Children().Append(subtitle_);
  controls::Grid::SetColumn(text_, 1);
  root_.Children().Append(text_);

  action_ = PackButton(
      context_.icons, context_.brushes, IconRole::kClose, L"", [this] {
        if (can_cancel_)
          context_.delegate.OnDownloadCommand(id_, DownloadCommand::kCancel);
        else if (can_show_)
          context_.delegate.OnDownloadCommand(id_,
                                              DownloadCommand::kShowInFolder);
      });
  action_->control.Width(kStack.action_size);
  action_->control.Height(kStack.action_size);
  action_->control.Margin({0, 0, kStack.action_margin_end, 0});
  action_->control.VerticalAlignment(xaml::VerticalAlignment::Center);
  action_->control.Visibility(xaml::Visibility::Collapsed);
  controls::Grid::SetColumn(action_->control, 2);
  root_.Children().Append(action_->control);

  entered_ = root_.PointerEntered(winrt::auto_revoke, [this](auto&&, auto&&) {
    hovered_ = true;
    root_.Background(context_.brushes.hover);
    Refresh();
  });
  exited_ = root_.PointerExited(winrt::auto_revoke, [this](auto&&, auto&&) {
    hovered_ = false;
    root_.Background(context_.brushes.transparent);
    Refresh();
  });
  const auto cancel_press = [this](auto&&, auto&&) {
    pressed_ = {};
    released_ = {};
  };
  canceled_ = root_.PointerCanceled(winrt::auto_revoke, cancel_press);
  // Implicit capture can end between PointerReleased and Tapped. A completed
  // press/release remains eligible; a lost in-progress press does not.
  capture_lost_ = root_.PointerCaptureLost(
      winrt::auto_revoke, [this](auto&&, auto&&) { pressed_ = {}; });
  unloaded_ = root_.Unloaded(winrt::auto_revoke, cancel_press);
  pressed_event_ = root_.PointerPressed(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        pressed_ = {};
        released_ = {};
        if (id_ && can_open_ &&
            !IsWithin(args.OriginalSource(), action_->control)) {
          pressed_ = id_;
          pressed_pointer_ = args.Pointer().PointerId();
        }
      });
  released_event_ = root_.PointerReleased(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        const auto pressed = std::exchange(pressed_, DownloadId{});
        const auto at = args.GetCurrentPoint(root_).Position();
        if (pressed == id_ && pressed && can_open_ &&
            args.Pointer().PointerId() == pressed_pointer_ &&
            !IsWithin(args.OriginalSource(), action_->control) && at.X >= 0 &&
            at.Y >= 0 && at.X < root_.ActualWidth() &&
            at.Y < root_.ActualHeight())
          released_ = pressed;
      });
  tapped_ = root_.Tapped(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::TappedRoutedEventArgs& args) {
        const auto released = std::exchange(released_, DownloadId{});
        if (released == id_ && released && can_open_ &&
            !IsWithin(args.OriginalSource(), action_->control))
          context_.delegate.OnDownloadCommand(id_, DownloadCommand::kOpen);
      });
  context_menu_ = root_.RightTapped(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::RightTappedRoutedEventArgs& args) {
        pressed_ = {};
        released_ = {};
        if (!id_)
          return;
        context_.request_menu(
            MenuKind::kDownload, id_.value,
            bounds_ ? bounds_(root_) : context_.bounds(root_));
        args.Handled(true);
      });
  if (library_row)
    root_.Margin({0, 0, 0, kStack.entry_gap});
  SetText({});
}

void DownloadEntry::SetText(UiTextView text) {
  cancel_text_ = NativeText(UiText::kDownloadCancel, text);
  show_text_ = NativeText(UiText::kDownloadShow, text);
  Refresh();
}

void DownloadEntry::SetCompact(bool compact) {
  compact_ = compact;
  badge_.SetCompact(compact);
  text_.Visibility(compact ? xaml::Visibility::Collapsed
                           : xaml::Visibility::Visible);
  controls::Grid::SetColumnSpan(badge_.Root(), compact ? 3 : 1);
  Refresh();
}

void DownloadEntry::SetActionSpaceReserved(bool reserved) {
  // Hover reveals the folder/cancel button without changing the row's desired
  // width. The dropdown's natural width is the widest complete row.
  const xaml::GridLength width{
      reserved ? kStack.action_size + kStack.action_margin_end : 0,
      reserved ? xaml::GridUnitType::Pixel : xaml::GridUnitType::Auto};
  const auto column = root_.ColumnDefinitions().GetAt(2);
  if (column.Width() != width)
    column.Width(width);
}

void DownloadEntry::Update(const DownloadVm& download,
                           const LibraryPaletteVm& palette,
                           const media::ImageSource& picture) {
  if (id_ != download.id) {
    pressed_ = {};
    released_ = {};
  }
  id_ = download.id;
  can_open_ = download.can_open;
  can_cancel_ = download.can_cancel;
  can_show_ = download.can_show;
  badge_.Update(download, palette, picture);
  const auto title = winrt::to_hstring(download.file_name);
  if (title_.Text() != title) {
    title_.Text(title);
    Accessible(root_, title);
    SetToolTip(root_, title);
  }
  const auto status = winrt::to_hstring(download.status);
  if (subtitle_.Text() != status)
    subtitle_.Text(status);
  Refresh();
}

void DownloadEntry::Refresh() {
  // The one action cancels a download on its way in and shows a file that is
  // here, and only while the entry is hovered.
  const bool visible = hovered_ && !compact_ && (can_cancel_ || can_show_);
  const auto value =
      visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
  if (action_->control.Visibility() != value)
    action_->control.Visibility(value);
  action_->icon->SetRole(can_cancel_ ? IconRole::kClose : IconRole::kFolder);
  SetButtonText(*action_, can_cancel_ ? cancel_text_ : show_text_);
}
}  // namespace kr::win
