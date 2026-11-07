#include "download_stack.hpp"

#include <windows.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <limits>
#include <utility>

#include "glass_material.hpp"
#include "krypton/ui/layout/popup_layout.hpp"
#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
namespace {
namespace composition = winrt::Microsoft::UI::Composition;
using std::chrono::milliseconds;
constexpr auto& kStack = design::kTokens.download_stack;

// Zen's --zen-library-entry-easing: linear(0, .34 12%, .7 24%, .93 38%,
// 1.03 55%, 1).
constexpr std::array<std::pair<float, float>, 6> kEntryEasing{{
    {0.0f, 0.0f},
    {0.12f, 0.34f},
    {0.24f, 0.7f},
    {0.38f, 0.93f},
    {0.55f, 1.03f},
    {1.0f, 1.0f},
}};

}  // namespace

DownloadStack::DownloadStack(ViewContext& context,
                             const controls::Button& library_button,
                             const xaml::UIElement& tabs,
                             MediaStack& media,
                             const xaml::FrameworkElement& media_spacer)
    : context_(context),
      library_button_(library_button),
      tabs_(tabs),
      media_(media),
      media_spacer_(media_spacer) {
  list_.Orientation(controls::Orientation::Vertical);
  list_.Spacing(kStack.entry_gap);
  list_.Padding({0, kStack.padding_top, 0, 0});
  list_.VerticalAlignment(xaml::VerticalAlignment::Bottom);
  list_.IsHitTestVisible(false);
  list_.Background(context_.brushes.transparent);

  // The button's badge, lifted to Zen's 14px,-8px spot and scaled to 0.8
  // while downloading. It never takes input.
  button_badge_ = std::make_unique<DownloadBadge>(context_);
  button_badge_->SetCompact(true);
  badge_.Children().Append(button_badge_->Root());
  const float ring = kStack.pending_size + 2 * kStack.pending_border;
  badge_.Width(ring);
  badge_.Height(ring);
  const double box =
      kTokens.spacing.button_size + 2 * kTokens.spacing.button_margin;
  badge_host_.Width(box);
  badge_host_.Height(box);
  badge_host_.IsHitTestVisible(false);
  badge_host_.HorizontalAlignment(xaml::HorizontalAlignment::Left);
  badge_host_.VerticalAlignment(xaml::VerticalAlignment::Center);
  badge_host_.Children().Append(badge_);
  // Zen's transform-origin is the badge's top left, 14px,-8px from the
  // button's corner.
  controls::Canvas::SetLeft(
      badge_, kTokens.spacing.button_margin + kStack.button_badge_x);
  controls::Canvas::SetTop(
      badge_, kTokens.spacing.button_margin + kStack.button_badge_y);
  badge_.Scale({kStack.button_badge_scale, kStack.button_badge_scale, 1});
  badge_host_.Visibility(xaml::Visibility::Collapsed);

  close_timer_ = list_.DispatcherQueue().CreateTimer();
  close_timer_.Interval(Ms(kStack.close_delay_ms));
  close_timer_.IsRepeating(false);
  tick_ = close_timer_.Tick(winrt::auto_revoke, [this](auto&&, auto&&) {
    if (context_.pointer_over(library_button_) || PointerOverPreview() ||
        GetCapture() || GetKeyState(VK_LBUTTON) < 0 ||
        GetKeyState(VK_RBUTTON) < 0 || GetKeyState(VK_MBUTTON) < 0 ||
        GetKeyState(VK_XBUTTON1) < 0 || GetKeyState(VK_XBUTTON2) < 0)
      return;
    Close();
  });

  const auto refresh = [this](auto&&, auto&&) { context_.hover_changed(); };
  button_entered_ = library_button_.PointerEntered(winrt::auto_revoke, refresh);
  list_entered_ = list_.PointerEntered(winrt::auto_revoke, refresh);
  button_exited_ = library_button_.PointerExited(winrt::auto_revoke, refresh);
  list_exited_ = list_.PointerExited(winrt::auto_revoke, refresh);
  list_sized_ = list_.SizeChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
    UpdateClip();
    RefreshDropdownBackground();
  });
}

DownloadStack::~DownloadStack() {
  button_entered_.revoke();
  button_exited_.revoke();
  list_entered_.revoke();
  list_exited_.revoke();
  dropdown_entered_.revoke();
  dropdown_exited_.revoke();
  list_sized_.revoke();
  dropdown_closed_.revoke();
  dropdown_opened_.revoke();
  try {
    if (dropdown_presenter_)
      context_.glass.Place(dropdown_presenter_, {});
    if (dropdown_)
      dropdown_.Hide();
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Download dropdown could not hide at teardown");
  }
  tick_.revoke();
  if (close_timer_)
    close_timer_.Stop();
}

void DownloadStack::EnsureEntries() {
  if (entries_[0])
    return;
  for (auto& entry : entries_) {
    entry = std::make_unique<DownloadEntry>(context_, false);
    entry->SetCompact(compact_);
    entry->SetActionSpaceReserved(horizontal_);
    entry->SetText(text_);
    entry->Root().Opacity(0);
    entry->Root().Translation({0, kStack.entry_offset, 0});
    list_.Children().Append(entry->Root());
  }
}

void DownloadStack::Render(const ShellViewModel& vm) {
  chrome_visible_ =
      horizontal_ ? vm.layout.toolbar_visible : vm.layout.sidebar_visible;
  if (horizontal_) {
    const float inset = kTokens.menu.padding + kTokens.menu.border_width;
    const double maximum =
        std::max(0.0f, PopupViewport(vm.client).width - 2 * inset);
    if (list_.MaxWidth() != maximum)
      list_.MaxWidth(maximum);
  }
  if (dropdown_ && vm.changed.Has(Dirty::kTheme)) {
    dropdown_style_ = nullptr;
    if (horizontal_ && open_) {
      StyleDropdown();
      RefreshDropdownBackground();
    }
  }
  const auto& library = vm.library;
  library_open_ = library.open;
  const auto& stack = library.stack;
  const auto previous_shown = shown_;
  shown_ = stack.size();
  if (shown_)
    EnsureEntries();
  if (entries_[0]) {
    const auto unused = entries_.size() - shown_;
    for (std::size_t slot = 0; slot < entries_.size(); ++slot) {
      auto& entry = *entries_[slot];
      const bool visible = slot >= unused;
      const auto value =
          visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
      if (entry.Root().Visibility() != value)
        entry.Root().Visibility(value);
      if (!visible)
        continue;
      const auto& download = stack[slot - unused];
      entry.Update(download, library.palette,
                   download.icon ? context_.images.Get(download.icon, vm.images)
                                 : media::ImageSource{nullptr});
    }
  }
  downloading_ = library.downloading;
  if (downloading_)
    button_badge_->Update(stack.back(), library.palette, nullptr);
  badge_host_.Visibility(downloading_ && !open_ ? xaml::Visibility::Visible
                                                : xaml::Visibility::Collapsed);
  if (horizontal_ && library_open_) {
    DismissHover();
    return;
  }
  if (open_ && !shown_)
    Close();
  context_.hover_changed();
  if (open_) {
    if (shown_ > previous_shown)
      Animate(true, previous_shown);
    UpdateClip();
  }
}

void DownloadStack::SetCompact(bool compact) {
  compact_ = compact;
  badge_host_.HorizontalAlignment(compact ? xaml::HorizontalAlignment::Center
                                          : xaml::HorizontalAlignment::Left);
  for (auto& entry : entries_)
    if (entry)
      entry->SetCompact(compact);
}

void DownloadStack::SetHorizontal(bool horizontal,
                                  const controls::Grid& sidebar) {
  if (horizontal_ == horizontal)
    return;
  Close();
  dismissed_hover_ = true;
  if (horizontal) {
    if (clip_) {
      clip_.StopAnimation(L"BottomInset");
      clip_.BottomInset(0);
      clipped_ = 0;
    }
    std::uint32_t index;
    if (sidebar.Children().IndexOf(list_, index))
      sidebar.Children().RemoveAt(index);
    if (!dropdown_) {
      dropdown_ = controls::Flyout();
      // Entries already animate on the compositor. A second native popup
      // transition can retain the old input window during a focus handoff.
      dropdown_.AreOpenCloseAnimationsEnabled(false);
      dropdown_.ShouldConstrainToRootBounds(false);
      dropdown_closed_ = dropdown_.Closed(
          winrt::auto_revoke, [this](auto&&, auto&&) { DropdownClosed(); });
      dropdown_opened_ = dropdown_.Opened(
          winrt::auto_revoke,
          [this](auto&&, auto&&) { RefreshDropdownBackground(); });
    }
    list_.Width(std::numeric_limits<double>::quiet_NaN());
    list_.MaxWidth(std::numeric_limits<double>::infinity());
    list_.Padding(UniformThickness(0));
    list_.VerticalAlignment(xaml::VerticalAlignment::Top);
    dropdown_.Content(list_);
    SetCompact(false);
  } else {
    dropdown_.Content(nullptr);
    list_.Width(std::numeric_limits<double>::quiet_NaN());
    list_.MaxWidth(std::numeric_limits<double>::infinity());
    list_.Padding({0, kStack.padding_top, 0, 0});
    list_.VerticalAlignment(xaml::VerticalAlignment::Bottom);
    sidebar.Children().Append(list_);
  }
  horizontal_ = horizontal;
  for (auto& entry : entries_)
    if (entry)
      entry->SetActionSpaceReserved(horizontal);
}

void DownloadStack::StyleDropdown() {
  const auto theme = library_button_.ActualTheme();
  if (!dropdown_style_ || theme != dropdown_theme_) {
    dropdown_theme_ = theme;
    dropdown_style_ =
        xaml::Style(winrt::xaml_typename<controls::FlyoutPresenter>());
    const auto setters = dropdown_style_.Setters();
    setters.Append(
        xaml::Setter(xaml::FrameworkElement::RequestedThemeProperty(),
                     winrt::box_value(theme)));
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
    dropdown_.FlyoutPresenterStyle(dropdown_style_);
  }
  dropdown_.SystemBackdrop(
      dropdown_material_.For(context_.brushes.menu_backdrop_enabled(),
                             context_.brushes.menu_material()));
}

void DownloadStack::RefreshDropdownBackground() noexcept try {
  if (!horizontal_ || !open_)
    return;
  if (!dropdown_presenter_) {
    for (auto parent = media::VisualTreeHelper::GetParent(list_); parent;
         parent = media::VisualTreeHelper::GetParent(parent))
      if (auto presenter = parent.try_as<controls::FlyoutPresenter>()) {
        dropdown_presenter_ = presenter;
        const auto refresh = [this](auto&&, auto&&) {
          context_.hover_changed();
        };
        dropdown_entered_ =
            presenter.PointerEntered(winrt::auto_revoke, refresh);
        dropdown_exited_ = presenter.PointerExited(winrt::auto_revoke, refresh);
        break;
      }
  }
  if (!dropdown_presenter_ || dropdown_presenter_.ActualWidth() <= 0 ||
      dropdown_presenter_.ActualHeight() <= 0)
    return;
  const auto bounds = context_.bounds(dropdown_presenter_);
  const auto brush =
      context_.glass.For(dropdown_presenter_, GlassRole::kMenu,
                         context_.brushes.menu_background, kTokens.menu.radius);
  context_.glass.Place(dropdown_presenter_, bounds);
  if (dropdown_presenter_.Background() != brush)
    dropdown_presenter_.Background(brush);
} catch (...) {
  // Retain the native menu fallback if popup layout is not available yet.
}

void DownloadStack::SetText(UiTextView text) {
  text_ = text;
  for (auto& entry : entries_)
    if (entry)
      entry->SetText(text);
}

void DownloadStack::Open() noexcept try {
  if (open_ || !shown_ || !context_.pointer_over(library_button_) ||
      dismissed_hover_ || (horizontal_ && closing_) || !CanOpen())
    return;
  // ShowAt can synchronously route pointer events back into this object.
  open_ = true;
  context_.delegate.OnDownloadStackOpened();
  list_.IsHitTestVisible(true);
  badge_host_.Visibility(xaml::Visibility::Collapsed);
  if (horizontal_) {
    StyleDropdown();
    controls::Primitives::FlyoutShowOptions options;
    options.Placement(
        controls::Primitives::FlyoutPlacementMode::BottomEdgeAlignedRight);
    options.ShowMode(controls::Primitives::FlyoutShowMode::Transient);
    if (dropdown_.XamlRoot() != library_button_.XamlRoot())
      dropdown_.XamlRoot(library_button_.XamlRoot());
    dropdown_.ShowAt(library_button_, options);
  } else {
    media_.SetObscured(true);
  }
  if (!open_)
    return;
  RefreshDropdownBackground();
  Animate(true);
  UpdateClip();
  context_.hover_changed();
} catch (const winrt::hresult_error& error) {
  dismissed_hover_ = true;
  Close();
  platform::WriteDefaultLog(
      LogLevel::kWarning,
      std::format("Download hover opening: {} (0x{:08X})",
                  winrt::to_string(error.message()),
                  static_cast<std::uint32_t>(error.code().value)));
} catch (...) {
  dismissed_hover_ = true;
  Close();
  platform::WriteDefaultLog(LogLevel::kWarning,
                            "Download hover could not open");
}

void DownloadStack::Close() noexcept try {
  close_timer_.Stop();
  if (!open_)
    return;
  open_ = false;
  list_.IsHitTestVisible(false);
  badge_host_.Visibility(downloading_ ? xaml::Visibility::Visible
                                      : xaml::Visibility::Collapsed);
  if (horizontal_) {
    closing_ = true;
    dropdown_.Hide();
  } else
    media_.SetObscured(false);
  if (dropdown_presenter_) {
    dropdown_entered_.revoke();
    dropdown_exited_.revoke();
    context_.glass.Place(dropdown_presenter_, {});
    dropdown_presenter_ = nullptr;
  }
  Animate(false);
  UpdateClip();
  context_.hover_changed();
} catch (const winrt::hresult_error& error) {
  platform::WriteDefaultLog(
      LogLevel::kWarning,
      std::format("Download hover closing: {} (0x{:08X})",
                  winrt::to_string(error.message()),
                  static_cast<std::uint32_t>(error.code().value)));
} catch (...) {
  platform::WriteDefaultLog(LogLevel::kWarning,
                            "Download hover could not close");
}

void DownloadStack::ScheduleClose() {
  if (open_ && !close_timer_.IsRunning() &&
      !context_.pointer_over(library_button_) && !PointerOverPreview())
    close_timer_.Start();
}

void DownloadStack::DismissHover() noexcept {
  dismissed_hover_ = true;
  Close();
}

bool DownloadStack::PointerOverPreview() const {
  return dropdown_presenter_ ? context_.pointer_over(dropdown_presenter_)
                             : context_.pointer_over(list_);
}

bool DownloadStack::CanOpen() const {
  return chrome_visible_ && !(horizontal_ && library_open_) &&
         (!context_.hover_popup_allowed ||
          context_.hover_popup_allowed(list_, open_));
}

void DownloadStack::RefreshPointerHover() noexcept try {
  if (horizontal_ && closing_)
    return;
  const bool over_button = context_.pointer_over(library_button_);
  if (!over_button)
    dismissed_hover_ = false;
  if (!open_) {
    if (over_button && shown_)
      Open();
    return;
  }
  if (!CanOpen()) {
    DismissHover();
    return;
  }
  if (over_button) {
    close_timer_.Stop();
  } else if (open_) {
    if (PointerOverPreview())
      close_timer_.Stop();
    else
      ScheduleClose();
  }
} catch (...) {
  platform::WriteDefaultLog(LogLevel::kWarning,
                            "Download hover state could not refresh");
}

void DownloadStack::DropdownClosed() noexcept try {
  // Do not let an old native close detach a subsequently restored sidebar list.
  const bool dismissed = !closing_;
  closing_ = false;
  if (horizontal_) {
    if (dismissed)
      dismissed_hover_ = context_.pointer_over(library_button_);
    open_ = false;
    close_timer_.Stop();
    list_.IsHitTestVisible(false);
    dropdown_entered_.revoke();
    dropdown_exited_.revoke();
    if (dropdown_presenter_) {
      context_.glass.Place(dropdown_presenter_, {});
      dropdown_presenter_ = nullptr;
    }
    badge_host_.Visibility(downloading_ ? xaml::Visibility::Visible
                                        : xaml::Visibility::Collapsed);
    if (dismissed)
      Animate(false);
  }
  context_.hover_changed();
} catch (...) {
  platform::WriteDefaultLog(LogLevel::kWarning,
                            "Download dropdown close could not settle");
}

void DownloadStack::Animate(bool open, std::size_t already_visible) {
  if (!entries_[0])
    return;
  const bool motion = context_.motion.Enabled(context_.brushes.high_contrast());
  composition::Compositor compositor{nullptr};
  if (motion) {
    compositor = CompositorFor(list_);
    if (!linear_)
      linear_ = compositor.CreateLinearEasingFunction();
  }
  // The newest entry, nearest the button, leads; each one above follows 12ms
  // later, like Zen's --zen-library-entry-index.
  std::size_t index = 0;
  for (std::size_t slot = entries_.size(); slot-- > 0;) {
    if (open && slot >= entries_.size() - already_visible)
      continue;
    const auto& root = entries_[slot]->Root();
    if (root.Visibility() != xaml::Visibility::Visible)
      continue;
    const float from = open ? kStack.entry_offset : 0.0f;
    const float to = open ? 0.0f : kStack.entry_offset;
    root.Translation({0, to, 0});
    root.Opacity(open ? 1.0f : 0.0f);
    if (!motion)
      continue;
    auto translate = compositor.CreateVector3KeyFrameAnimation();
    auto fade = compositor.CreateScalarKeyFrameAnimation();
    translate.Target(L"Translation");
    fade.Target(L"Opacity");
    for (const auto& [time, progress] : kEntryEasing) {
      translate.InsertKeyFrame(time, {0, from + (to - from) * progress, 0},
                               linear_);
      fade.InsertKeyFrame(
          time, std::clamp(open ? progress : 1.0f - progress, 0.0f, 1.0f),
          linear_);
    }
    for (const composition::KeyFrameAnimation& animation :
         {translate.as<composition::KeyFrameAnimation>(),
          fade.as<composition::KeyFrameAnimation>()}) {
      animation.Duration(Ms(kStack.entry_ms));
      animation.DelayTime(
          Ms(kStack.entry_stagger_ms * static_cast<float>(index)));
      animation.DelayBehavior(
          composition::AnimationDelayBehavior::SetInitialValueBeforeDelay);
    }
    root.StartAnimation(translate);
    root.StartAnimation(fade);
    ++index;
  }
}

// Zen masks the tab list down to the stack. A compositor inset clip keeps
// tab rows from showing through the entries; it follows the stack's height.
void DownloadStack::UpdateClip() noexcept try {
  if (horizontal_)
    return;
  const auto visual =
      xaml::Hosting::ElementCompositionPreview::GetElementVisual(tabs_);
  if (!clip_) {
    if (!open_)
      return;
    clip_ = visual.Compositor().CreateInsetClip();
    visual.Clip(clip_);
  }
  // The list is bottom-aligned over the media row; the tabs end above it.
  // Cut where the first entry starts, past the list's top padding.
  const float covered =
      open_ ? std::max(0.0f,
                       static_cast<float>(list_.ActualHeight()) -
                           static_cast<float>(media_spacer_.ActualHeight()) -
                           kStack.mask_overshoot)
            : 0.0f;
  if (covered == clipped_)
    return;
  clipped_ = covered;
  if (!context_.motion.Enabled(context_.brushes.high_contrast())) {
    clip_.BottomInset(covered);
    return;
  }
  const auto compositor = visual.Compositor();
  if (!open_easing_)
    open_easing_ = compositor.CreateCubicBezierEasingFunction({0.22f, 1.0f},
                                                              {0.36f, 1.0f});
  auto animation = compositor.CreateScalarKeyFrameAnimation();
  animation.InsertExpressionKeyFrame(0, L"this.StartingValue");
  animation.InsertKeyFrame(1, covered, open_easing_);
  animation.Duration(Ms(kStack.open_ms));
  clip_.StartAnimation(L"BottomInset", animation);
} catch (const winrt::hresult_error& error) {
  platform::WriteDefaultLog(
      LogLevel::kWarning,
      std::format("Download hover clip: {} (0x{:08X})",
                  winrt::to_string(error.message()),
                  static_cast<std::uint32_t>(error.code().value)));
} catch (...) {
  platform::WriteDefaultLog(LogLevel::kWarning,
                            "Download hover clip could not update");
}
}  // namespace kr::win
