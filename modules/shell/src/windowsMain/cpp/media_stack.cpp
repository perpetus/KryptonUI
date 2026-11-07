#include "media_stack.hpp"

#include <windows.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <algorithm>
#include <cstdio>
#include <limits>

#include "krypton/ui/layout/popup_layout.hpp"
#include "krypton/ui/platform/platform.hpp"
#include "library_visuals.hpp"
#include "media_slider.hpp"
#include "native_theme.hpp"

namespace kr::win {
namespace {
namespace composition = winrt::Microsoft::UI::Composition;
using Clock = std::chrono::steady_clock;
constexpr auto& kMedia = design::kTokens.media_controls;

double CardHeight() {
  return 2 * kMedia.card_padding_block + kMedia.button_size + 2;
}

void Show(const xaml::UIElement& element, bool visible) {
  const auto value =
      visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
  if (element.Visibility() != value)
    element.Visibility(value);
}

void SetMediaText(const controls::TextBlock& block, std::string_view text) {
  const auto value = winrt::to_hstring(text);
  if (block.Text() != value)
    block.Text(value);
}

}  // namespace

struct MediaStack::Card {
  Card(ViewContext& context, MediaStack& stack);
  ~Card();
  void Update(const MediaCardVm& vm,
              const LibraryPaletteVm& palette,
              const ImageStore* images,
              bool dark);
  void SetText(UiTextView text);
  void Reveal(bool expanded, bool animate);
  void Tick(Clock::time_point now);
  [[nodiscard]] bool Ticking() const noexcept {
    return playing && position_visible && duration > 0;
  }
  std::unique_ptr<NativeButton> Button(IconRole role,
                                       MediaCommand command,
                                       const controls::Panel& parent);
  void Seek();

  ViewContext& context;
  MediaId id;
  controls::Border root;
  controls::Grid main, info, progress, controls_row, playback;
  controls::StackPanel titles, buttons, devices;
  controls::TextBlock title, artist, current, total;
  controls::Slider slider;
  controls::Grid focus_content;
  controls::Image favicon;
  media::Brush progress_brush{nullptr};
  std::unique_ptr<IconView> screen;
  std::unique_ptr<NativeButton> pip, close, focus, previous, play, next, mute,
      microphone, camera;
  winrt::hstring play_text, pause_text, mute_text, unmute_text;
  double base = 0, duration = 0, rate = 1, reported = -1;
  Clock::time_point stamp;
  bool playing = false, sharing = false, position_visible = false;
  bool can_seek = false, updating = false, dragging = false, muted = false;
  xaml::Input::PointerEventHandler pressed{nullptr}, released{nullptr};
  controls::Primitives::RangeBase::ValueChanged_revoker value_changed;
  xaml::UIElement::Tapped_revoker tapped;
  xaml::UIElement::PointerEntered_revoker focus_entered;
  xaml::UIElement::PointerExited_revoker focus_exited;
};

MediaStack::Card::Card(ViewContext& context, MediaStack& stack)
    : context(context) {
  root.Padding({kMedia.card_padding_inline, kMedia.card_padding_block,
                kMedia.card_padding_inline, kMedia.card_padding_block});
  root.CornerRadius(UniformCorners(kMedia.card_radius));
  root.BorderThickness(UniformThickness(1));
  root.Child(main);
  for (int row = 0; row < 3; ++row)
    AddRow(main, 0, xaml::GridUnitType::Auto);

  // Title row, shown on hover: title and artist, then PiP and close.
  AddColumn(info, 1, xaml::GridUnitType::Star);
  AddColumn(info, 0, xaml::GridUnitType::Auto);
  info.Padding(
      {kMedia.info_padding, kMedia.info_padding, 0, kMedia.info_padding});
  title = Label(L"", kTokens.type.size_workspace);
  title.FontWeight(kMedium);
  title.Foreground(context.brushes.text);
  artist = Label(L"", kTokens.type.size_small);
  artist.Foreground(context.brushes.text);
  artist.Opacity(kMedia.artist_opacity);
  titles.Children().Append(title);
  titles.Children().Append(artist);
  info.Children().Append(titles);
  buttons.Orientation(controls::Orientation::Horizontal);
  buttons.Spacing(kMedia.button_spacing);
  buttons.VerticalAlignment(xaml::VerticalAlignment::Top);
  controls::Grid::SetColumn(buttons, 1);
  info.Children().Append(buttons);
  pip = Button(IconRole::kPictureInPicture,
               MediaCommand::kTogglePictureInPicture, buttons);
  close = Button(IconRole::kClose, MediaCommand::kClose, buttons);
  main.Children().Append(info);

  // Position row, shown on hover.
  AddColumn(progress, 0, xaml::GridUnitType::Auto);
  AddColumn(progress, 1, xaml::GridUnitType::Star);
  AddColumn(progress, 0, xaml::GridUnitType::Auto);
  progress.Height(kMedia.progress_row_height);
  for (auto* time : {&current, &total}) {
    *time = Label(L"0:00", kMedia.time_size);
    time->FontWeight(kMedium);
    time->Opacity(kMedia.time_opacity);
    time->Foreground(context.brushes.text);
  }
  controls::Grid::SetColumn(slider, 1);
  controls::Grid::SetColumn(total, 2);
  slider.Margin({kMedia.progress_margin, 0, kMedia.progress_margin, 0});
  slider.VerticalAlignment(xaml::VerticalAlignment::Center);
  slider.IsThumbToolTipEnabled(false);
  slider.Minimum(0);
  slider.StepFrequency(0.1);
  SetMediaSliderResources(slider, context.brushes.pill);
  progress.Children().Append(current);
  progress.Children().Append(slider);
  progress.Children().Append(total);
  controls::Grid::SetRow(progress, 1);
  main.Children().Append(progress);

  // Controls: the page's icon, playback, mute; or microphone and camera
  // while the page captures.
  AddColumn(controls_row, 0, xaml::GridUnitType::Auto);
  AddColumn(controls_row, 1, xaml::GridUnitType::Star);
  AddColumn(controls_row, 0, xaml::GridUnitType::Auto);
  AddColumn(controls_row, 0, xaml::GridUnitType::Auto);
  controls_row.ColumnSpacing(kMedia.controls_gap);
  focus = IconButton(context.brushes, L"", L"", [this] {
    this->context.delegate.OnMediaCommand(id, MediaCommand::kFocus);
  });
  favicon.Width(kTokens.spacing.icon_size);
  favicon.Height(kTokens.spacing.icon_size);
  screen = std::make_unique<IconView>(context.icons, IconRole::kScreen,
                                      kTokens.spacing.icon_size,
                                      context.brushes.icon);
  screen->Root().Visibility(xaml::Visibility::Collapsed);
  focus_content.Children().Append(favicon);
  focus_content.Children().Append(screen->Root());
  focus->control.Content(focus_content);
  focus->control.Width(kMedia.button_size);
  focus->control.Height(kMedia.button_size);
  focus->control.Margin(UniformThickness(0));
  focus->control.CornerRadius(UniformCorners(kMedia.button_radius));
  focus_entered =
      focus->control.PointerEntered(winrt::auto_revoke, [this](auto&&, auto&&) {
        favicon.Visibility(xaml::Visibility::Collapsed);
        screen->Root().Visibility(xaml::Visibility::Visible);
      });
  focus_exited =
      focus->control.PointerExited(winrt::auto_revoke, [this](auto&&, auto&&) {
        favicon.Visibility(xaml::Visibility::Visible);
        screen->Root().Visibility(xaml::Visibility::Collapsed);
      });
  controls_row.Children().Append(focus->control);

  playback.MaxWidth(kMedia.playback_max_width);
  AddColumn(playback, 0, xaml::GridUnitType::Auto);
  AddColumn(playback, 1, xaml::GridUnitType::Star);
  AddColumn(playback, 0, xaml::GridUnitType::Auto);
  controls::Grid::SetColumn(playback, 1);
  previous = Button(IconRole::kPrevious, MediaCommand::kPrevious, playback);
  play = Button(IconRole::kPlay, MediaCommand::kPlayPause, playback);
  next = Button(IconRole::kNext, MediaCommand::kNext, playback);
  controls::Grid::SetColumn(play->control, 1);
  controls::Grid::SetColumn(next->control, 2);
  play->control.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  controls_row.Children().Append(playback);
  mute = Button(IconRole::kSpeaker, MediaCommand::kToggleMute, controls_row);
  controls::Grid::SetColumn(mute->control, 2);
  devices.Orientation(controls::Orientation::Horizontal);
  devices.Spacing(kMedia.button_spacing);
  controls::Grid::SetColumn(devices, 3);
  microphone =
      Button(IconRole::kMicrophone, MediaCommand::kToggleMicrophone, devices);
  camera = Button(IconRole::kCamera, MediaCommand::kToggleCamera, devices);
  controls_row.Children().Append(devices);
  controls::Grid::SetRow(controls_row, 2);
  main.Children().Append(controls_row);

  // Seeks commit when a drag ends, like Zen's change event; keyboard and
  // clicks commit at once.
  pressed = [this](auto&&, auto&&) { dragging = true; };
  released = [this, &stack](auto&&, auto&&) {
    if (!dragging)
      return;
    dragging = false;
    Seek();
    stack.ScheduleClose();
    this->context.hover_changed();
  };
  slider.AddHandler(xaml::UIElement::PointerPressedEvent(),
                    winrt::box_value(pressed), true);
  slider.AddHandler(xaml::UIElement::PointerReleasedEvent(),
                    winrt::box_value(released), true);
  slider.AddHandler(xaml::UIElement::PointerCaptureLostEvent(),
                    winrt::box_value(released), true);
  value_changed = slider.ValueChanged(
      winrt::auto_revoke,
      [this](auto&&,
             const controls::Primitives::RangeBaseValueChangedEventArgs& args) {
        if (updating)
          return;
        current.Text(winrt::to_hstring(FormatMediaTime(args.NewValue())));
        if (!dragging)
          Seek();
      });
  tapped = root.Tapped(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::TappedRoutedEventArgs& args) {
        // Like Zen: anywhere but a button or the position row shows the tab.
        const auto source = args.OriginalSource();
        if (IsWithin(source, progress))
          return;
        for (const auto* button :
             {pip.get(), close.get(), focus.get(), previous.get(), play.get(),
              next.get(), mute.get(), microphone.get(), camera.get()})
          if (IsWithin(source, button->control))
            return;
        this->context.delegate.OnMediaCommand(id, MediaCommand::kFocus);
      });
  Reveal(false, false);
}

MediaStack::Card::~Card() {
  slider.RemoveHandler(xaml::UIElement::PointerPressedEvent(),
                       winrt::box_value(pressed));
  slider.RemoveHandler(xaml::UIElement::PointerReleasedEvent(),
                       winrt::box_value(released));
  slider.RemoveHandler(xaml::UIElement::PointerCaptureLostEvent(),
                       winrt::box_value(released));
}

std::unique_ptr<NativeButton> MediaStack::Card::Button(
    IconRole role,
    MediaCommand command,
    const controls::Panel& parent) {
  auto button = PackButton(
      context.icons, context.brushes, role, L"",
      [this, command] { context.delegate.OnMediaCommand(id, command); });
  button->control.Width(kMedia.button_size);
  button->control.Height(kMedia.button_size);
  button->control.Margin(UniformThickness(0));
  button->control.CornerRadius(UniformCorners(kMedia.button_radius));
  parent.Children().Append(button->control);
  return button;
}

void MediaStack::Card::Seek() {
  if (!can_seek)
    return;
  base = slider.Value();
  stamp = Clock::now();
  context.delegate.OnMediaSeek(id, base);
}

void MediaStack::Card::SetText(UiTextView text) {
  play_text = NativeText(UiText::kMediaPlay, text);
  pause_text = NativeText(UiText::kMediaPause, text);
  mute_text = NativeText(UiText::kMediaMute, text);
  unmute_text = NativeText(UiText::kMediaUnmute, text);
  SetButtonText(*pip, NativeText(UiText::kMediaPictureInPicture, text));
  SetButtonText(*close, NativeText(UiText::kMediaClose, text));
  SetButtonText(*focus, NativeText(UiText::kMediaFocus, text));
  SetButtonText(*previous, NativeText(UiText::kMediaPrevious, text));
  SetButtonText(*next, NativeText(UiText::kMediaNext, text));
  SetButtonText(*microphone, NativeText(UiText::kMediaMicrophone, text));
  SetButtonText(*camera, NativeText(UiText::kMediaCamera, text));
  SetButtonText(*play, playing ? pause_text : play_text);
  SetButtonText(*mute, muted ? unmute_text : mute_text);
  Accessible(slider, NativeText(UiText::kMediaSeek, text));
}

void MediaStack::Card::Update(const MediaCardVm& vm,
                              const LibraryPaletteVm& palette,
                              const ImageStore* images,
                              bool dark) {
  const bool moved = vm.id != id || vm.position != reported ||
                     vm.playing != playing || vm.playback_rate != rate;
  id = vm.id;
  sharing = vm.sharing;
  playing = vm.playing;
  muted = vm.muted;
  can_seek = vm.can_seek;
  position_visible = vm.position_visible;
  rate = vm.playback_rate;
  duration = vm.duration;
  if (moved) {
    reported = vm.position;
    base = vm.position;
    stamp = Clock::now();
  }
  root.Background(context.brushes.For(palette.card));
  root.BorderBrush(dark ? context.brushes.For(palette.card_outline)
                        : context.brushes.transparent);
  SetMediaText(title, vm.title);
  SetMediaText(artist, vm.artist);
  Show(artist, !vm.artist.empty());
  Accessible(root, title.Text());
  favicon.Source(vm.icon ? context.images.Get(vm.icon, images)
                         : media::ImageSource{nullptr});
  play->icon->SetRole(playing ? IconRole::kPause : IconRole::kPlay);
  SetButtonText(*play, playing ? pause_text : play_text);
  mute->icon->SetRole(muted ? IconRole::kSpeakerOff : IconRole::kSpeaker);
  SetButtonText(*mute, muted ? unmute_text : mute_text);
  play->control.IsEnabled(vm.can_play_pause);
  previous->control.IsEnabled(vm.can_previous);
  next->control.IsEnabled(vm.can_next);
  slider.IsEnabled(vm.can_seek);
  Show(pip->control, vm.can_picture_in_picture && !sharing);
  Show(playback, !sharing);
  Show(mute->control, !sharing);
  Show(devices, sharing);
  const auto red = context.brushes.For(Color{kMedia.muted_red});
  microphone->icon->SetRole(vm.microphone_muted ? IconRole::kMicrophoneOff
                                                : IconRole::kMicrophone);
  microphone->icon->SetBrush(vm.microphone_muted ? red : context.brushes.icon);
  camera->icon->SetRole(vm.camera_muted ? IconRole::kCameraOff
                                        : IconRole::kCamera);
  camera->icon->SetBrush(vm.camera_muted ? red : context.brushes.icon);
  microphone->control.IsEnabled(vm.can_toggle_microphone);
  camera->control.IsEnabled(vm.can_toggle_camera);
  const auto next_progress_brush = context.brushes.For(palette.progress);
  if (progress_brush != next_progress_brush) {
    progress_brush = next_progress_brush;
    const auto resources = slider.Resources();
    for (const auto& key :
         {L"SliderTrackValueFill", L"SliderTrackValueFillPointerOver",
          L"SliderTrackValueFillPressed", L"SliderThumbBackground",
          L"SliderThumbBackgroundPointerOver", L"SliderThumbBackgroundPressed",
          L"SliderOuterThumbBackground"})
      resources.Insert(winrt::box_value(winrt::hstring(key)), progress_brush);
  }
  updating = true;
  if (slider.Maximum() != std::max(duration, 0.1))
    slider.Maximum(std::max(duration, 0.1));
  updating = false;
  total.Text(winrt::to_hstring(FormatMediaTime(duration)));
  Tick(Clock::now());
}

void MediaStack::Card::Reveal(bool expanded, bool animate) {
  const bool show_progress = expanded && position_visible;
  Show(info, expanded);
  Show(progress, show_progress);
  if (!expanded || !animate)
    return;
  // GetElementVisual(root) hands transform ownership to the compositor and
  // prevents the stack from subsequently using root.Scale on pointer hover.
  const auto compositor =
      xaml::Media::CompositionTarget::GetCompositorForCurrentThread();
  const auto easing =
      compositor.CreateCubicBezierEasingFunction({0.25f, 1}, {0.5f, 1});
  for (const xaml::UIElement& row :
       {info.as<xaml::UIElement>(), progress.as<xaml::UIElement>()}) {
    auto fade = compositor.CreateScalarKeyFrameAnimation();
    fade.Target(L"Opacity");
    fade.InsertKeyFrame(0, 0);
    fade.InsertKeyFrame(1, 1, easing);
    fade.Duration(Ms(kMedia.reveal_ms));
    auto rise = compositor.CreateVector3KeyFrameAnimation();
    rise.Target(L"Translation");
    rise.InsertKeyFrame(0, {0, kMedia.reveal_offset, 0});
    rise.InsertKeyFrame(1, {0, 0, 0}, easing);
    rise.Duration(Ms(kMedia.reveal_ms));
    row.StartAnimation(fade);
    row.StartAnimation(rise);
  }
}

void MediaStack::Card::Tick(Clock::time_point now) {
  double position = base;
  if (playing)
    position += std::chrono::duration<double>(now - stamp).count() * rate;
  position = std::clamp(position, 0.0, std::max(duration, 0.0));
  if (!dragging) {
    current.Text(winrt::to_hstring(FormatMediaTime(position)));
    updating = true;
    slider.Value(position);
    updating = false;
  }
}

MediaStack::MediaStack(ViewContext& context) : context_(context) {
  root_.Orientation(controls::Orientation::Vertical);
  root_.Spacing(kMedia.stack_gap);
  root_.VerticalAlignment(xaml::VerticalAlignment::Bottom);
  root_.Visibility(xaml::Visibility::Collapsed);
  spacer_.Height(0);
  spacer_.IsHitTestVisible(false);
  toolbar_button_ = IconButton(
      context_.brushes, L"\uE90B", NativeText(UiText::kLibraryMedia), [this] {
        OpenDropdown(
            toolbar_button_->control.FocusState() == xaml::FocusState::Keyboard,
            true);
      });
  Show(toolbar_button_->control, false);
  close_timer_ = root_.DispatcherQueue().CreateTimer();
  close_timer_.Interval(Ms(kMedia.hover_close_delay_ms));
  close_timer_.IsRepeating(false);
  close_tick_ = close_timer_.Tick(winrt::auto_revoke, [this](auto&&, auto&&) {
    if (dropdown_keyboard_ || PointerOverDropdown() ||
        context_.pointer_over(toolbar_button_->control) || GetCapture() ||
        GetKeyState(VK_LBUTTON) < 0 || GetKeyState(VK_RBUTTON) < 0 ||
        GetKeyState(VK_MBUTTON) < 0 || GetKeyState(VK_XBUTTON1) < 0 ||
        GetKeyState(VK_XBUTTON2) < 0)
      return;
    for (std::size_t slot = 0; slot < shown_; ++slot)
      if (cards_[slot]->dragging)
        return;
    CloseDropdown();
  });
  button_entered_ = toolbar_button_->control.PointerEntered(
      winrt::auto_revoke, [this](auto&&, auto&&) { context_.hover_changed(); });
  button_exited_ = toolbar_button_->control.PointerExited(
      winrt::auto_revoke, [this](auto&&, auto&&) { context_.hover_changed(); });
  entered_ = root_.PointerEntered(
      winrt::auto_revoke, [this](auto&&, auto&&) { context_.hover_changed(); });
  exited_ = root_.PointerExited(
      winrt::auto_revoke, [this](auto&&, auto&&) { context_.hover_changed(); });
  clock_ = root_.DispatcherQueue().CreateTimer();
  clock_.Interval(std::chrono::seconds(1));
  tick_ = clock_.Tick(winrt::auto_revoke, [this](auto&&, auto&&) { Tick(); });
}

MediaStack::~MediaStack() {
  entered_.revoke();
  exited_.revoke();
  button_entered_.revoke();
  button_exited_.revoke();
  dropdown_entered_.revoke();
  dropdown_exited_.revoke();
  dropdown_sized_.revoke();
  dropdown_closed_.revoke();
  dropdown_opened_.revoke();
  CloseDropdown();
  close_tick_.revoke();
  if (close_timer_)
    close_timer_.Stop();
  tick_.revoke();
  if (clock_)
    clock_.Stop();
}

void MediaStack::Render(const ShellViewModel& vm) {
  chrome_visible_ =
      horizontal_ ? vm.layout.toolbar_visible : vm.layout.sidebar_visible;
  const auto& cards = vm.library.media;
  const auto count = std::min(cards.size(), cards_.size());
  const bool appeared = !shown_ && count;
  bool restack = shown_ != count;
  for (std::size_t slot = 0; slot < count; ++slot) {
    restack |= !cards_[slot] || cards_[slot]->id != cards[slot].id;
    if (!cards_[slot]) {
      cards_[slot] = std::make_unique<Card>(context_, *this);
      cards_[slot]->SetText(text_);
      // Older cards sit above newer ones: slot 0 is last, at the bottom.
      root_.Children().InsertAt(0, cards_[slot]->root);
    }
    // Tab activation can change which cards are shown without changing the
    // retained session. Reconcile the stack, but leave unchanged card content
    // and slider resources alone.
    if (cards_[slot]->id != cards[slot].id ||
        vm.changed.HasAny(Dirty::kLibrary | Dirty::kTabs | Dirty::kImages |
                          Dirty::kTheme | Dirty::kText | Dirty::kLayout))
      cards_[slot]->Update(cards[slot], vm.library.palette, vm.images,
                           vm.theme.dark);
    cards_[slot]->Reveal(expanded_, false);
  }
  for (std::size_t slot = 0; slot < cards_.size(); ++slot)
    if (cards_[slot])
      Show(cards_[slot]->root, slot < count);
  shown_ = count;
  dropdown_allowed_ =
      vm.layout.toolbar_visible && !vm.library.open && !vm.command_bar.open;
  if (!shown_ || !dropdown_allowed_)
    CloseDropdown();
  UpdateVisibility();
  if (horizontal_) {
    const auto viewport = PopupViewport(vm.client);
    const float inset = kTokens.menu.padding + kTokens.menu.border_width;
    const double width = std::min<double>(
        kMedia.dropdown_width, std::max(0.0f, viewport.width - 2 * inset));
    if (root_.Width() != width)
      root_.Width(width);
    const double height = std::max(0.0f, viewport.height - 2 * inset);
    if (dropdown_scroll_.MaxHeight() != height)
      dropdown_scroll_.MaxHeight(height);
    const auto direction = vm.text.right_to_left
                               ? xaml::FlowDirection::RightToLeft
                               : xaml::FlowDirection::LeftToRight;
    if (root_.FlowDirection() != direction)
      root_.FlowDirection(direction);
    if (vm.changed.Has(Dirty::kTheme)) {
      dropdown_style_ = nullptr;
      if (dropdown_open_) {
        StyleDropdown();
        RefreshDropdownBackground();
      }
    }
  }
  if (restack || vm.changed.Has(Dirty::kTheme))
    Restack(restack && !appeared);
  UpdateClock();
  if (appeared && !horizontal_ &&
      context_.motion.Enabled(context_.brushes.high_contrast())) {
    // Zen's showMediaControls: rise 10px into place while fading in.
    const auto compositor =
        xaml::Media::CompositionTarget::GetCompositorForCurrentThread();
    auto fade = compositor.CreateScalarKeyFrameAnimation();
    fade.Target(L"Opacity");
    fade.InsertKeyFrame(0, 0);
    fade.InsertKeyFrame(1, 1);
    fade.Duration(Ms(kMedia.reveal_ms));
    auto rise = compositor.CreateVector3KeyFrameAnimation();
    rise.Target(L"Translation");
    rise.InsertKeyFrame(0, {0, kMedia.appear_offset, 0});
    rise.InsertKeyFrame(1, {0, 0, 0});
    rise.Duration(Ms(kMedia.reveal_ms));
    root_.StartAnimation(fade);
    root_.StartAnimation(rise);
  }
  context_.hover_changed();
}

void MediaStack::SetCompact(bool compact) {
  // Zen hides media controls while the sidebar is collapsed.
  compact_ = compact;
  UpdateVisibility();
  if (compact && !horizontal_)
    Expand(false);
  UpdateClock();
}

void MediaStack::UpdateVisibility() {
  Show(toolbar_button_->control, horizontal_ && shown_);
  Show(root_, shown_ && (horizontal_ || !compact_));
  const double reserved =
      shown_ && !horizontal_ && !compact_
          ? CardHeight() +
                kMedia.peek * std::min<double>(static_cast<double>(shown_ - 1),
                                               kMedia.max_peek)
          : 0;
  if (spacer_.Height() != reserved)
    spacer_.Height(reserved);
}

void MediaStack::SetHorizontal(bool horizontal, const controls::Grid& sidebar) {
  if (horizontal_ == horizontal)
    return;
  CloseDropdown();
  Expand(false);
  dismissed_hover_ = true;
  if (horizontal) {
    if (!dropdown_) {
      dropdown_ = controls::Flyout();
      // The cards own their motion; close the native input window promptly
      // instead of retaining another popup transition above the next menu.
      dropdown_.AreOpenCloseAnimationsEnabled(false);
      dropdown_.ShouldConstrainToRootBounds(false);
      dropdown_scroll_ = controls::ScrollViewer();
      dropdown_scroll_.HorizontalScrollMode(controls::ScrollMode::Disabled);
      dropdown_scroll_.HorizontalScrollBarVisibility(
          controls::ScrollBarVisibility::Disabled);
      dropdown_scroll_.VerticalScrollBarVisibility(
          controls::ScrollBarVisibility::Auto);
      dropdown_.Content(dropdown_scroll_);
      dropdown_closed_ = dropdown_.Closed(
          winrt::auto_revoke, [this](auto&&, auto&&) { DropdownClosed(); });
      dropdown_opened_ = dropdown_.Opened(
          winrt::auto_revoke,
          [this](auto&&, auto&&) { RefreshDropdownBackground(); });
      dropdown_sized_ = dropdown_scroll_.SizeChanged(
          winrt::auto_revoke,
          [this](auto&&, auto&&) { RefreshDropdownBackground(); });
    }
    // Visual Parent may be null before first layout. Use the known owner.
    std::uint32_t index;
    if (sidebar.Children().IndexOf(root_, index))
      sidebar.Children().RemoveAt(index);
    root_.VerticalAlignment(xaml::VerticalAlignment::Top);
    dropdown_scroll_.Content(root_);
  } else {
    dropdown_scroll_.Content(nullptr);
    root_.Width(std::numeric_limits<double>::quiet_NaN());
    root_.VerticalAlignment(xaml::VerticalAlignment::Bottom);
    sidebar.Children().Append(root_);
  }
  horizontal_ = horizontal;
  UpdateVisibility();
  Restack(false);
  UpdateClock();
}

void MediaStack::StyleDropdown() {
  if (!dropdown_style_) {
    dropdown_style_ =
        xaml::Style(winrt::xaml_typename<controls::FlyoutPresenter>());
    const auto setters = dropdown_style_.Setters();
    setters.Append(
        xaml::Setter(xaml::FrameworkElement::RequestedThemeProperty(),
                     winrt::box_value(toolbar_button_->control.ActualTheme())));
    setters.Append(xaml::Setter(controls::Control::ForegroundProperty(),
                                context_.brushes.text));
    setters.Append(xaml::Setter(controls::Control::BackgroundProperty(),
                                context_.brushes.menu_background));
    setters.Append(xaml::Setter(controls::Control::BorderBrushProperty(),
                                context_.brushes.menu_border));
    setters.Append(xaml::Setter(
        controls::Control::BorderThicknessProperty(),
        winrt::box_value(UniformThickness(kTokens.menu.border_width))));
    setters.Append(
        xaml::Setter(controls::Control::CornerRadiusProperty(),
                     winrt::box_value(UniformCorners(kTokens.menu.radius))));
    setters.Append(
        xaml::Setter(controls::Control::PaddingProperty(),
                     winrt::box_value(UniformThickness(kTokens.menu.padding))));
    setters.Append(xaml::Setter(xaml::FrameworkElement::MinWidthProperty(),
                                winrt::box_value(0.0)));
    setters.Append(xaml::Setter(xaml::FrameworkElement::MinHeightProperty(),
                                winrt::box_value(0.0)));
    setters.Append(xaml::Setter(
        xaml::FrameworkElement::MaxWidthProperty(),
        winrt::box_value(std::numeric_limits<double>::infinity())));
  }
  dropdown_.FlyoutPresenterStyle(dropdown_style_);
  dropdown_.SystemBackdrop(
      dropdown_material_.For(context_.brushes.menu_backdrop_enabled(),
                             context_.brushes.menu_material()));
}

void MediaStack::RefreshDropdownBackground() noexcept try {
  if (!dropdown_open_)
    return;
  if (!dropdown_presenter_)
    for (auto parent = media::VisualTreeHelper::GetParent(root_); parent;
         parent = media::VisualTreeHelper::GetParent(parent))
      if (auto presenter = parent.try_as<controls::FlyoutPresenter>()) {
        dropdown_presenter_ = presenter;
        dropdown_entered_ = presenter.PointerEntered(
            winrt::auto_revoke,
            [this](auto&&, auto&&) { context_.hover_changed(); });
        dropdown_exited_ = presenter.PointerExited(
            winrt::auto_revoke,
            [this](auto&&, auto&&) { context_.hover_changed(); });
        break;
      }
  if (!dropdown_presenter_ || dropdown_presenter_.ActualWidth() <= 0 ||
      dropdown_presenter_.ActualHeight() <= 0)
    return;
  const auto bounds = context_.bounds(dropdown_presenter_);
  const auto brush =
      context_.glass.For(dropdown_presenter_, GlassRole::kMenu,
                         context_.brushes.menu_background, kTokens.menu.radius);
  if (dropdown_presenter_.Background() != brush)
    dropdown_presenter_.Background(brush);
  context_.glass.Place(dropdown_presenter_, bounds);
} catch (...) {
  // A closing presenter no longer has a backdrop to update.
}

void MediaStack::OpenDropdown(bool keyboard, bool explicit_request) noexcept
    try {
  close_timer_.Stop();
  if (!horizontal_ || !shown_ || !dropdown_allowed_ ||
      !toolbar_button_->control.IsLoaded() || closing_)
    return;
  if (explicit_request && context_.dismiss_hover_popups)
    context_.dismiss_hover_popups(root_);
  if ((!explicit_request &&
       (dismissed_hover_ ||
        !context_.pointer_over(toolbar_button_->control))) ||
      !CanOpen(explicit_request))
    return;
  if (explicit_request)
    dismissed_hover_ = false;
  if (dropdown_open_) {
    if (keyboard) {
      dropdown_keyboard_ = true;
      const auto& card = *cards_[0];
      const auto& button = card.sharing ? card.focus : card.play;
      if (!button->control.Focus(xaml::FocusState::Keyboard))
        (void)card.close->control.Focus(xaml::FocusState::Keyboard);
    }
    return;
  }
  // Pointer routing and native close callbacks may reenter ShowAt.
  dropdown_open_ = true;
  dropdown_keyboard_ = keyboard;
  Expand(true);
  StyleDropdown();
  controls::Primitives::FlyoutShowOptions options;
  options.Placement(
      controls::Primitives::FlyoutPlacementMode::BottomEdgeAlignedRight);
  options.ShowMode(keyboard ? controls::Primitives::FlyoutShowMode::Standard
                            : controls::Primitives::FlyoutShowMode::Transient);
  if (dropdown_.XamlRoot() != toolbar_button_->control.XamlRoot())
    dropdown_.XamlRoot(toolbar_button_->control.XamlRoot());
  dropdown_.ShowAt(toolbar_button_->control, options);
  if (!dropdown_open_)
    return;
  UpdateClock();
  RefreshDropdownBackground();
  context_.hover_changed();
} catch (...) {
  dismissed_hover_ = true;
  CloseDropdown();
  Expand(false);
  platform::WriteDefaultLog(LogLevel::kWarning, "Media hover could not open");
}

void MediaStack::CloseDropdown() noexcept try {
  close_timer_.Stop();
  if (!dropdown_open_)
    return;
  dropdown_open_ = false;
  dropdown_keyboard_ = false;
  closing_ = true;
  dropdown_entered_.revoke();
  dropdown_exited_.revoke();
  Expand(false);
  dropdown_.Hide();
  if (dropdown_presenter_) {
    context_.glass.Place(dropdown_presenter_, {});
    dropdown_presenter_ = nullptr;
  }
  context_.hover_changed();
} catch (...) {
  platform::WriteDefaultLog(LogLevel::kWarning, "Media hover could not close");
}

void MediaStack::ScheduleClose() {
  if (dropdown_open_ && !dropdown_keyboard_ && !close_timer_.IsRunning() &&
      !PointerOverDropdown() &&
      !context_.pointer_over(toolbar_button_->control))
    close_timer_.Start();
}

void MediaStack::DismissHover() noexcept {
  dismissed_hover_ = true;
  CloseDropdown();
  if (!horizontal_)
    Expand(false);
}

bool MediaStack::PointerOverDropdown() const {
  return dropdown_presenter_ ? context_.pointer_over(dropdown_presenter_)
                             : context_.pointer_over(root_);
}

bool MediaStack::CanOpen(bool explicit_request) const {
  return chrome_visible_ &&
         (!context_.hover_popup_allowed ||
          context_.hover_popup_allowed(
              root_, dropdown_open_ || expanded_ || explicit_request));
}

void MediaStack::RefreshPointerHover() noexcept try {
  if (!horizontal_) {
    const bool over = shown_ && !compact_ && root_.IsHitTestVisible() &&
                      context_.pointer_over(root_);
    Expand(over && CanOpen());
    return;
  }
  if (closing_)
    return;
  const bool over_button = context_.pointer_over(toolbar_button_->control);
  if (!over_button)
    dismissed_hover_ = false;
  if (!dropdown_open_) {
    if (over_button && shown_ && dropdown_allowed_)
      OpenDropdown();
    return;
  }
  if (!dropdown_allowed_ || !CanOpen()) {
    DismissHover();
    return;
  }
  if (over_button) {
    close_timer_.Stop();
  } else if (dropdown_open_ && !dropdown_keyboard_) {
    if (PointerOverDropdown())
      close_timer_.Stop();
    else
      ScheduleClose();
  }
} catch (...) {
  platform::WriteDefaultLog(LogLevel::kWarning,
                            "Media hover state could not refresh");
}

void MediaStack::DropdownClosed() noexcept try {
  const bool dismissed = !closing_;
  closing_ = false;
  if (horizontal_) {
    if (dismissed)
      dismissed_hover_ = context_.pointer_over(toolbar_button_->control);
    dropdown_open_ = false;
    dropdown_keyboard_ = false;
    close_timer_.Stop();
    Expand(false);
    dropdown_entered_.revoke();
    dropdown_exited_.revoke();
    if (dropdown_presenter_) {
      context_.glass.Place(dropdown_presenter_, {});
      dropdown_presenter_ = nullptr;
    }
  }
  context_.hover_changed();
} catch (...) {
  platform::WriteDefaultLog(LogLevel::kWarning,
                            "Media dropdown close could not settle");
}

void MediaStack::SetObscured(bool obscured) {
  if (obscured)
    Expand(false);
  if (!root_.OpacityTransition())
    root_.OpacityTransition(xaml::ScalarTransition());
  root_.OpacityTransition().Duration(
      Ms(context_.motion.Enabled(context_.brushes.high_contrast())
             ? kMedia.reveal_ms
             : 0));
  root_.IsHitTestVisible(!obscured);
  root_.Opacity(obscured ? 0 : 1);
}

void MediaStack::SetText(UiTextView text) {
  text_ = text;
  SetButtonText(*toolbar_button_, NativeText(UiText::kLibraryMedia, text));
  for (auto& card : cards_)
    if (card)
      card->SetText(text);
}

void MediaStack::Expand(bool expanded) noexcept {
  if (expanded_ == expanded)
    return;
  try {
    expanded_ = expanded;
    // Like Zen's card ::after bridge, the expanded stack's gaps must retain
    // hover. Leave the collapsed stack's transformed-away area noninteractive.
    root_.Background(expanded ? context_.brushes.transparent
                              : media::Brush{nullptr});
    const bool animate =
        context_.motion.Enabled(context_.brushes.high_contrast());
    for (std::size_t slot = 0; slot < shown_; ++slot)
      cards_[slot]->Reveal(expanded, animate);
    Restack(animate);
    UpdateClock();
    if (expanded)
      Tick();
    return;
  } catch (const winrt::hresult_error& error) {
    char message[96]{};
    std::snprintf(message, sizeof(message),
                  "Media hover expansion failed (0x%08X)",
                  static_cast<unsigned>(error.code().value));
    platform::WriteDefaultLog(LogLevel::kWarning, message);
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Media hover expansion failed");
  }
  // A rejected native animation must not escape through a XAML event. Return
  // the retained cards to the collapsed state without starting more animation.
  expanded_ = false;
  try {
    root_.Background(nullptr);
    clock_.Stop();
    for (std::size_t slot = 0; slot < shown_; ++slot)
      cards_[slot]->Reveal(false, false);
    Restack(false);
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Media hover recovery could not complete");
  }
}

void MediaStack::Restack(bool animate) {
  animate =
      animate && context_.motion.Enabled(context_.brushes.high_contrast());
  composition::Compositor compositor{nullptr};
  composition::CubicBezierEasingFunction easing{nullptr};
  if (animate) {
    compositor =
        xaml::Media::CompositionTarget::GetCompositorForCurrentThread();
    easing = compositor.CreateCubicBezierEasingFunction({0.25f, 1}, {0.5f, 1});
  }
  const double height = CardHeight();
  for (std::size_t slot = 0; slot < shown_; ++slot) {
    const auto& root = cards_[slot]->root;
    // Collapsed, each card sits on the front card's spot, peeking out above
    // it; expanded, each takes its own place in the column.
    const float level = expanded_
                            ? 0.0f
                            : static_cast<float>(std::min<double>(
                                  static_cast<double>(slot), kMedia.max_peek));
    const float offset =
        expanded_ ? 0.0f
                  : static_cast<float>(slot * (height + kMedia.stack_gap) -
                                       kMedia.peek * level);
    const float scale = 1 - kMedia.peek_scale * level;
    const float opacity = 1 - kMedia.peek_opacity * level;
    root.CenterPoint({static_cast<float>(root.ActualWidth()) / 2, 0, 0});
    if (!animate) {
      root.Translation({0, offset, 0});
      root.Scale({scale, scale, 1});
      root.Opacity(opacity);
      continue;
    }
    auto move = compositor.CreateVector3KeyFrameAnimation();
    move.Target(L"Translation");
    move.InsertExpressionKeyFrame(0, L"this.StartingValue");
    move.InsertKeyFrame(1, {0, offset, 0}, easing);
    move.Duration(Ms(kMedia.stack_ms));
    auto size = compositor.CreateVector3KeyFrameAnimation();
    size.Target(L"Scale");
    size.InsertExpressionKeyFrame(0, L"this.StartingValue");
    size.InsertKeyFrame(1, {scale, scale, 1}, easing);
    size.Duration(Ms(kMedia.stack_ms));
    auto fade = compositor.CreateScalarKeyFrameAnimation();
    fade.Target(L"Opacity");
    fade.InsertExpressionKeyFrame(0, L"this.StartingValue");
    fade.InsertKeyFrame(1, opacity, easing);
    fade.Duration(Ms(kMedia.reveal_ms));
    root.Translation({0, offset, 0});
    root.Scale({scale, scale, 1});
    root.Opacity(opacity);
    root.StartAnimation(move);
    root.StartAnimation(size);
    root.StartAnimation(fade);
  }
}

void MediaStack::Tick() {
  const auto now = Clock::now();
  for (std::size_t slot = 0; slot < shown_; ++slot)
    cards_[slot]->Tick(now);
}

// Times only show while expanded, so the clock never runs otherwise.
void MediaStack::UpdateClock() {
  bool ticking = false;
  for (std::size_t slot = 0; slot < shown_; ++slot)
    ticking |= cards_[slot]->Ticking();
  const bool run =
      expanded_ && ticking && (horizontal_ ? dropdown_open_ : !compact_);
  if (clock_.IsRunning() == run)
    return;
  if (run)
    clock_.Start();
  else
    clock_.Stop();
}
}  // namespace kr::win
