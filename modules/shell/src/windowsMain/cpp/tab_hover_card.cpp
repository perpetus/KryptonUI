#include "tab_hover_card.hpp"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

#include <chrono>
#include <utility>

#include "tab_preview_surface.hpp"

namespace kr::win {
using namespace controls;
using xaml::Visibility;

namespace {
void SetLabelText(const TextBlock& block,
                  std::string& cached,
                  std::string_view text) {
  if (cached == text)
    return;
  cached.assign(text);
  block.Text(winrt::to_hstring(text));
}

void SetImage(const controls::Image& image, const media::ImageSource& source) {
  if (image.Source() != source)
    image.Source(source);
  image.Visibility(source ? Visibility::Visible : Visibility::Collapsed);
}
}  // namespace

TabHoverCard::TabHoverCard(ViewContext& context,
                           const xaml::FrameworkElement& target,
                           const xaml::UIElement& focus)
    : context_(context),
      target_(target),
      focus_(focus ? focus : target.as<xaml::UIElement>()) {
  entered_ = target.PointerEntered(winrt::auto_revoke,
                                   [this](auto&&, auto&&) { Schedule(); });
  exited_ = target.PointerExited(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        // Exits from the tab's own buttons bubble here; only leaving the tab
        // counts. Leaving the island for the page reports the last position,
        // still inside the tab, so a mouse also checks where the cursor is.
        const auto target = target_.get();
        if (!target)
          return;
        const auto at = args.GetCurrentPoint(target).Position();
        const bool inside = at.X >= 0 && at.Y >= 0 &&
                            at.X < target.ActualWidth() &&
                            at.Y < target.ActualHeight();
        if (inside &&
            (args.Pointer().PointerDeviceType() !=
                 winrt::Microsoft::UI::Input::PointerDeviceType::Mouse ||
             context_.pointer_over(target)))
          return;
        if (IsOpen())
          context_.tab_previews.NoteExit();
        Close();
      });
  pressed_ = [this](auto&&, auto&&) { Close(); };
  target.AddHandler(xaml::UIElement::PointerPressedEvent(),
                    winrt::box_value(pressed_), true);
  const auto keyboard = focus_.get();
  focused_ = keyboard.GotFocus(winrt::auto_revoke, [this](auto&&, auto&&) {
    const auto element = focus_.get();
    if (element && element.FocusState() == xaml::FocusState::Keyboard)
      Open();
  });
  blurred_ = keyboard.LostFocus(winrt::auto_revoke,
                                [this](auto&&, auto&&) { Close(); });
}

TabHoverCard::~TabHoverCard() {
  if (const auto target = target_.get())
    target.RemoveHandler(xaml::UIElement::PointerPressedEvent(),
                         winrt::box_value(pressed_));
  tick_.revoke();
  Close();
}

void TabHoverCard::SetTarget(TabId tab,
                             std::string_view title,
                             std::string_view url) {
  if (tab_ != tab)
    Close();
  tab_ = tab;
  target_title_ = winrt::to_hstring(title);
  target_domain_ = winrt::to_hstring(TabPreviewDomain(url));
}

void TabHoverCard::Schedule() {
  if (!tab_ || IsOpen() || (timer_ && timer_.IsRunning()))
    return;
  const auto delay = context_.tab_previews.ShowDelay();
  if (delay <= std::chrono::milliseconds::zero()) {
    Open();
    return;
  }
  if (!timer_) {
    const auto target = target_.get();
    if (!target)
      return;
    timer_ = target.DispatcherQueue().CreateTimer();
    timer_.IsRepeating(false);
    // A missed exit must not open a card for a tab the pointer has left.
    tick_ = timer_.Tick(winrt::auto_revoke, [this](auto&&, auto&&) {
      if (const auto target = target_.get();
          target && context_.pointer_over(target))
        Open();
    });
  }
  timer_.Interval(delay);
  timer_.Start();
}

void TabHoverCard::Open() {
  if (timer_)
    timer_.Stop();
  if (!tab_ || IsOpen())
    return;
  Build();
  title_.Text(target_title_);
  domain_.Text(target_domain_);
  memory_.Text(L"");
  status_.Text(L"");
  status_.Visibility(Visibility::Collapsed);
  title_text_.clear();
  domain_text_.clear();
  memory_text_.clear();
  status_text_.clear();
  text_current_ = false;
  request_id_ = context_.delegate.OnTabPreviewRequested(tab_);
  context_.hover_changed();
}

void TabHoverCard::SetText(UiTextView text) {
  unavailable_ = NativeText(UiText::kPreviewUnavailable, text);
  page_preview_ = NativeText(UiText::kPagePreview, text);
  text_current_ = true;
  if (card_) {
    Accessible(placeholder_, unavailable_);
    Accessible(thumbnail_, page_preview_);
  }
}

void TabHoverCard::EndRequest() {
  if (const auto request = std::exchange(request_id_, 0u)) {
    context_.tab_previews.Close(request);
    context_.delegate.OnTabPreviewDismissed(request);
    context_.hover_changed();
  }
  if (card_) {
    SetImage(favicon_, nullptr);
    SetImage(thumbnail_, nullptr);
    placeholder_.Visibility(Visibility::Visible);
  }
}

void TabHoverCard::Close() {
  if (timer_)
    timer_.Stop();
  EndRequest();
}

void TabHoverCard::Build() {
  if (card_)
    return;
  const auto& tokens = kTokens.tab_hover;
  auto& brushes = context_.brushes;
  card_ = Border();
  card_.Width(metrics_.width);
  card_.Background(brushes.preview_background);
  card_.BorderBrush(brushes.preview_border);
  card_.BorderThickness(UniformThickness(tokens.border_width));
  card_.CornerRadius(UniformCorners(tokens.radius));
  Grid content;
  for (int row = 0; row < 3; ++row)
    AddRow(content, 0, xaml::GridUnitType::Auto);
  card_.Child(content);

  StackPanel header;
  header.Padding(UniformThickness(tokens.padding));
  header.Spacing(tokens.title_gap);
  title_ = Label(L"", tokens.title_size);
  title_.FontWeight(kSemiBold);
  title_.Foreground(brushes.text);
  title_.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
  Grid title_row;
  AddColumn(title_row, 0, xaml::GridUnitType::Auto);
  AddColumn(title_row, 1, xaml::GridUnitType::Star);
  favicon_ = controls::Image();
  favicon_.Width(kTokens.spacing.icon_size);
  favicon_.Height(kTokens.spacing.icon_size);
  favicon_.Margin({0, 0, kTokens.spacing.tab_icon_gap, 0});
  favicon_.IsHitTestVisible(false);
  title_row.Children().Append(favicon_);
  Grid::SetColumn(title_, 1);
  title_row.Children().Append(title_);
  domain_ = Label(L"", tokens.detail_size);
  domain_.Foreground(brushes.muted);
  domain_.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
  header.Children().Append(title_row);
  header.Children().Append(domain_);
  content.Children().Append(header);

  viewport_ = Border();
  viewport_.Height(metrics_.thumbnail_height);
  viewport_.Background(brushes.preview_thumbnail);
  Grid preview;
  thumbnail_ = controls::Image();
  thumbnail_.Stretch(media::Stretch::UniformToFill);
  Accessible(thumbnail_, page_preview_);
  // Without a preview the card shows a globe, as the tab strip does.
  auto placeholder = SourceIcon(brushes.text, design::icons::kGlobe);
  placeholder.Width(tokens.placeholder_icon_size);
  placeholder.Height(tokens.placeholder_icon_size);
  placeholder.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  placeholder.VerticalAlignment(xaml::VerticalAlignment::Center);
  placeholder_ = placeholder;
  Accessible(placeholder_, unavailable_);
  preview.Children().Append(thumbnail_);
  preview.Children().Append(placeholder_);
  viewport_.Child(preview);
  Grid::SetRow(viewport_, 1);
  content.Children().Append(viewport_);

  footer_ = Border();
  footer_.Background(brushes.preview_footer);
  footer_.Padding({tokens.footer_padding_inline, tokens.footer_padding_block,
                   tokens.footer_padding_inline, tokens.footer_padding_block});
  StackPanel details;
  details.Spacing(tokens.status_gap);
  memory_ = Label(L"", tokens.detail_size);
  memory_.Foreground(brushes.muted);
  memory_.TextWrapping(xaml::TextWrapping::Wrap);
  details.Children().Append(memory_);
  status_ = Label(L"", tokens.detail_size);
  status_.Foreground(brushes.muted);
  status_.TextWrapping(xaml::TextWrapping::Wrap);
  details.Children().Append(status_);
  footer_.Child(details);
  Grid::SetRow(footer_, 2);
  content.Children().Append(footer_);
}

void TabHoverCard::Configure(TabPreviewMetrics metrics) {
  if (metrics_ == metrics)
    return;
  metrics_ = metrics;
  if (card_) {
    card_.Width(metrics.width);
    viewport_.Height(metrics.thumbnail_height);
  }
}

void TabHoverCard::Render(const ShellViewModel& vm) {
  if (!request_id_)
    return;
  const auto& preview = vm.tab_preview;
  if (preview.tab != tab_ || preview.request_id != request_id_) {
    Close();
    return;
  }
  const auto target = target_.get();
  const bool horizontal = vm.layout.tabs != TabLayout::kVertical;
  if (!target ||
      !(horizontal ? vm.layout.tab_strip_visible : vm.layout.sidebar_visible)) {
    Close();
    return;
  }
  const auto anchor = context_.bounds(target);
  const Rect visible_tabs =
      horizontal ? vm.layout.tab_strip : vm.layout.sidebar;
  if (anchor.right() <= visible_tabs.x || anchor.x >= visible_tabs.right() ||
      anchor.bottom() <= visible_tabs.y || anchor.y >= visible_tabs.bottom()) {
    Close();
    return;
  }
  const auto area =
      vm.layout.tabs == TabLayout::kVertical
          ? PlaceTabPreview(vm.client, vm.layout.sidebar, anchor,
                            vm.client.height, vm.layout.sidebar_on_right)
          : PlaceHorizontalTabPreview(vm.client, vm.layout.toolbar, anchor,
                                      vm.client.height, vm.text.right_to_left);
  if (area.empty()) {
    Close();
    return;
  }
  Configure(MeasureTabPreview(area.width));
  if (!text_current_ || vm.changed.Has(Dirty::kText))
    SetText(vm.text);
  SetLabelText(title_, title_text_, preview.title);
  SetLabelText(domain_, domain_text_, preview.domain);
  domain_.Visibility(preview.domain.empty() ? Visibility::Collapsed
                                            : Visibility::Visible);
  const auto favicon = context_.images.Get(preview.favicon, vm.images);
  SetImage(favicon_, favicon);
  favicon_.Visibility(favicon ? Visibility::Visible : Visibility::Collapsed);
  SetLabelText(memory_, memory_text_, preview.memory_text);
  viewport_.Visibility(preview.show_image ? Visibility::Visible
                                          : Visibility::Collapsed);
  footer_.Visibility(preview.memory_text.empty() && preview.status.empty()
                         ? Visibility::Collapsed
                         : Visibility::Visible);
  SetLabelText(status_, status_text_, preview.status);
  status_.Visibility(preview.status.empty() ? Visibility::Collapsed
                                            : Visibility::Visible);
  const auto thumbnail = context_.images.Get(preview.thumbnail, vm.images);
  SetImage(thumbnail_, thumbnail);
  placeholder_.Visibility(thumbnail ? Visibility::Collapsed
                                    : Visibility::Visible);
  const media::Brush backdrop = thumbnail ? context_.brushes.preview_thumbnail
                                          : context_.brushes.preview_background;
  if (viewport_.Background() != backdrop)
    viewport_.Background(backdrop);
  context_.tab_previews.Render(request_id_, card_, vm, anchor);
}

}  // namespace kr::win
