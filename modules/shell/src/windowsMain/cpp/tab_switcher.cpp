#include "tab_switcher.hpp"

#include <winrt/Microsoft.UI.Dispatching.h>

#include <algorithm>
#include <cmath>
#include <format>

#include "krypton/ui/platform/platform.hpp"
#include "krypton/ui/presentation/tab_switcher.hpp"

namespace kr::win {
namespace {
constexpr auto& t = design::kTokens.tab_switcher;
void Show(const xaml::UIElement& element, bool visible) {
  const auto visibility =
      visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
  if (element.Visibility() != visibility)
    element.Visibility(visibility);
}
}  // namespace
TabSwitcher::~TabSwitcher() {
  lifetime_.reset();
  loaded_.revoke();
  closed_.revoke();
  opened_.revoke();
  background_tapped_.revoke();
  Close();
}
void TabSwitcher::Ensure() {
  if (popup_)
    return;
  island_.Ensure();
  const auto root = island_.Root();
  context_.brushes.Install(root);
  popup_ = controls::Primitives::Popup();
  popup_.ShouldConstrainToRootBounds(false);
  popup_.IsLightDismissEnabled(true);
  // The native backdrop follows the popup's full page-sized bounds. The row
  // is a centred child, so the material blurs the page rather than just cards.
  overlay_ = controls::Grid();
  surface_ = controls::Border();
  surface_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  surface_.VerticalAlignment(xaml::VerticalAlignment::Center);
  surface_.CornerRadius(UniformCorners(t.radius));
  surface_.Padding(UniformThickness(t.padding));
  controls::StackPanel content;
  content.Spacing(t.header_gap);
  heading_ = Label(L"", kTokens.type.size_workspace);
  heading_.FontWeight(kSemiBold);
  content.Children().Append(heading_);
  cards_ = controls::Grid();
  cards_.RowSpacing(t.gap);
  cards_.ColumnSpacing(t.gap);
  for (std::size_t i = 0; i < cards.size(); ++i) {
    auto& card = cards[i];
    card.button = controls::Button();
    card.button.MinWidth(0);
    card.button.Padding(UniformThickness(t.card_padding));
    card.button.CornerRadius(UniformCorners(t.card_radius));
    card.button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    card.button.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
    card.button.BorderThickness(UniformThickness(t.border_width));
    controls::StackPanel body;
    controls::Grid header;
    header.Height(t.title_height);
    header.ColumnSpacing(t.card_padding);
    AddColumn(header, t.icon_size, xaml::GridUnitType::Pixel);
    AddColumn(header, 1, xaml::GridUnitType::Star);
    card.favicon = controls::Image();
    card.favicon.Width(t.icon_size);
    card.favicon.Height(t.icon_size);
    card.favicon.IsHitTestVisible(false);
    header.Children().Append(card.favicon);
    card.title = Label(L"", kTokens.type.size);
    card.title.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
    card.title.VerticalAlignment(xaml::VerticalAlignment::Center);
    controls::Grid::SetColumn(card.title, 1);
    header.Children().Append(card.title);
    body.Children().Append(header);
    card.image = controls::Grid();
    card.image.CornerRadius(UniformCorners(t.card_radius));
    card.preview = controls::Image();
    card.preview.Stretch(media::Stretch::Uniform);
    card.preview.IsHitTestVisible(false);
    card.image.Children().Append(card.preview);
    card.placeholder = Label(L"", kTokens.type.size);
    card.placeholder.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    card.placeholder.VerticalAlignment(xaml::VerticalAlignment::Center);
    card.placeholder.TextWrapping(xaml::TextWrapping::Wrap);
    card.placeholder.Margin(UniformThickness(t.card_padding));
    card.image.Children().Append(card.placeholder);
    body.Children().Append(card.image);
    card.button.Content(body);
    card.click =
        card.button.Click(winrt::auto_revoke, [this, i](auto&&, auto&&) {
          if (open_ && cards[i].id) {
            const auto index = first_ + i;
            Close();
            context_.delegate.OnTabSwitcherAccepted(index);
          }
        });
    cards_.Children().Append(card.button);
  }
  content.Children().Append(cards_);
  hint_ = Label(L"", kTokens.type.size_small);
  hint_.TextWrapping(xaml::TextWrapping::Wrap);
  content.Children().Append(hint_);
  surface_.Child(content);
  overlay_.Children().Append(surface_);
  popup_.Child(overlay_);
  root.Children().Append(popup_);
  const std::weak_ptr<int> weak = lifetime_;
  background_tapped_ = overlay_.Tapped(
      winrt::auto_revoke,
      [this, weak](auto&&, const xaml::Input::TappedRoutedEventArgs& args) {
        if (weak.expired() || args.OriginalSource() != overlay_)
          return;
        args.Handled(true);
        const auto generation = generation_;
        overlay_.DispatcherQueue().TryEnqueue([this, weak, generation] {
          if (weak.expired() || !open_ || generation != generation_)
            return;
          Close();
          context_.delegate.OnTabSwitcherDismissed();
        });
      });
  loaded_ = root.Loaded(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
    if (!weak.expired())
      OpenWhenLoaded();
  });
  opened_ = popup_.Opened(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
    if (!weak.expired())
      OpenWhenLoaded();
  });
  closed_ = popup_.Closed(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
    if (weak.expired())
      return;
    const auto generation = generation_;
    // Keep ownership until the native popup has closed. Do not let an old
    // callback hide a new switcher, or reenter model/render work from closure.
    closing_ = true;
    island_.Root().DispatcherQueue().TryEnqueue([this, weak, generation] {
      if (weak.expired() || generation != generation_ || popup_.IsOpen())
        return;
      Close();
      closing_ = false;
      context_.delegate.OnTabSwitcherDismissed();
      closed_callback_();
    });
  });
}
void TabSwitcher::Render(const ShellViewModel& vm, xaml::ElementTheme theme) {
  const auto& model = vm.tab_switcher;
  if (!model.open) {
    Close();
    return;
  }
  Ensure();
  if (!open_) {
    ++generation_;
    open_ = true;
    closing_ = false;
    focus_pending_ = true;
  }
  if (selected_ != model.selected || first_ != model.first)
    focus_pending_ = true;
  selected_ = model.selected;
  first_ = model.first;
  const auto page = vm.layout.card.empty() ? vm.client : vm.layout.card;
  const auto metrics = MeasureTabSwitcher(page, model.count);
  const auto columns = metrics.columns;
  if (columns_ != columns || cards_.ColumnDefinitions().Size() == 0 ||
      cards_.RowDefinitions().Size() != 1) {
    columns_ = columns;
    cards_.ColumnDefinitions().Clear();
    cards_.RowDefinitions().Clear();
    for (int i = 0; i < columns; ++i)
      AddColumn(cards_, 1, xaml::GridUnitType::Star);
    AddRow(cards_, 0, xaml::GridUnitType::Auto);
  }
  surface_.Width(metrics.bounds.width);
  bounds_ = page;
  overlay_.Width(page.width);
  overlay_.Height(page.height);
  heading_.Text(winrt::to_hstring(
      std::format("{} · {} / {}", vm.text[UiText::kTabSwitcher],
                  model.first + model.selected + 1, model.total)));
  heading_.Foreground(context_.brushes.text);
  hint_.Text(NativeText(UiText::kTabSwitcherHint, vm.text));
  hint_.Foreground(context_.brushes.muted);
  Accessible(surface_, NativeText(UiText::kTabSwitcher, vm.text));
  surface_.FlowDirection(vm.text.right_to_left
                             ? xaml::FlowDirection::RightToLeft
                             : xaml::FlowDirection::LeftToRight);
  if (!backdrop_ && !vm.theme.high_contrast)
    backdrop_ = MakeTabSwitcherBackdrop();
  popup_.SystemBackdrop(vm.theme.high_contrast ? nullptr : backdrop_);
  overlay_.Background(backdrop_ && !vm.theme.high_contrast
                          ? context_.brushes.transparent
                          : context_.brushes.menu_background);
  surface_.Background(context_.brushes.transparent);
  for (std::size_t i = 0; i < cards.size(); ++i) {
    auto& card = cards[i];
    const bool visible = i < model.count;
    Show(card.button, visible);
    if (!visible) {
      card.id = {};
      card.preview.Source(nullptr);
      card.favicon.Source(nullptr);
      continue;
    }
    const auto& item = model.items[i];
    card.id = item.tab;
    controls::Grid::SetColumn(card.button, static_cast<int>(i));
    controls::Grid::SetRow(card.button, 0);
    card.title.Text(winrt::to_hstring(item.title));
    card.title.Foreground(context_.brushes.text);
    Accessible(card.button, winrt::to_hstring(item.title));
    const auto image = context_.images.Get(item.thumbnail, vm.images);
    if (card.preview.Source() != image)
      card.preview.Source(image);
    const auto icon = context_.images.Get(item.favicon, vm.images);
    if (card.favicon.Source() != icon)
      card.favicon.Source(icon);
    card.image.Height(metrics.image_height);
    card.image.Background(context_.brushes.preview_thumbnail);
    card.placeholder.Text(NativeText(
        item.sleeping ? UiText::kSleeping : UiText::kPreviewUnavailable,
        vm.text));
    card.placeholder.Foreground(context_.brushes.muted);
    Show(card.placeholder, !image);
    card.button.Background(context_.brushes.transparent);
    card.button.BorderBrush(i == selected_
                                ? context_.brushes.favorite_active_border
                                : context_.brushes.transparent);
  }
  island_.Show(vm.client, vm.scale_factor, theme);
  OpenWhenLoaded();
}
void TabSwitcher::OpenWhenLoaded() {
  if (!open_ || !island_.IsReady())
    return;
  popup_.HorizontalOffset(bounds_.x);
  popup_.VerticalOffset(bounds_.y);
  if (!popup_.IsOpen())
    popup_.IsOpen(true);
  if (focus_pending_ && selected_ < cards.size() &&
      cards[selected_].button.IsLoaded()) {
    if (cards[selected_].button.Focus(xaml::FocusState::Keyboard))
      focus_pending_ = false;
  }
}
void TabSwitcher::Close() noexcept try {
  if (!open_ && !closing_ && (!popup_ || !popup_.IsOpen()))
    return;
  open_ = false;
  focus_pending_ = false;
  if (popup_) {
    popup_.IsOpen(false);
    popup_.SystemBackdrop(nullptr);
    backdrop_ = nullptr;
    for (auto& card : cards) {
      card.id = {};
      card.preview.Source(nullptr);
      card.favicon.Source(nullptr);
    }
  }
  island_.Hide();
} catch (...) {
  platform::WriteDefaultLog(LogLevel::kWarning, "Tab switcher could not close");
}
}  // namespace kr::win
