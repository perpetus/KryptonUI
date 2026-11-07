#include "top_chrome.hpp"

#include <algorithm>

#include "icon_view.hpp"

namespace kr::win {
bool TopChrome::FocusNative(Element target) {
  return target == Element::kNewTab && new_tab_ &&
         new_tab_->control.IsEnabled() &&
         new_tab_->control.Visibility() == xaml::Visibility::Visible &&
         new_tab_->control.Focus(xaml::FocusState::Programmatic);
}
namespace {
void Remove(const controls::Grid& parent, const xaml::UIElement& element) {
  std::uint32_t index;
  if (parent.Children().IndexOf(element, index))
    parent.Children().RemoveAt(index);
}
}  // namespace

TopChrome::TopChrome(ViewContext& context, ChromeHeader& header, TabList& tabs)
    : context_(context), header_(header), tabs_(tabs) {
  AddRow(root_, 0, xaml::GridUnitType::Pixel);
  AddRow(root_, 0, xaml::GridUnitType::Pixel);
  root_.Children().Append(header_.Toolbar());
  root_.Children().Append(strip_);
  AddColumn(strip_, 1, xaml::GridUnitType::Star);
  AddColumn(strip_, 0, xaml::GridUnitType::Auto);
  new_tab_ = PackButton(context_.icons, context_.brushes, IconRole::kAdd,
                        NativeText(UiText::kNewTab),
                        [this] { context_.delegate.OnNewTab(); });
  controls::Grid::SetColumn(new_tab_->control, 1);
  new_tab_->control.VerticalAlignment(xaml::VerticalAlignment::Center);
  new_tab_->control.RenderTransform(new_tab_position_);
  strip_.Children().Append(new_tab_->control);
  tabs_.SetHorizontalExtentChanged([this](float extent) {
    tabs_extent_ = extent;
    PositionNewTab();
  });
}

TopChrome::~TopChrome() {
  tabs_.SetHorizontalExtentChanged({});
}

void TopChrome::PositionNewTab() {
  if (mode_ == TabLayout::kVertical)
    return;
  const double height = std::min<double>(
      kTokens.spacing.button_size,
      std::max(0.0f, new_tab_row_height_ - 2 * kTokens.spacing.button_margin));
  if (new_tab_->control.Height() != height)
    new_tab_->control.Height(height);
  const float offset =
      new_tab_in_strip_
          ? HorizontalNewTabOffset(
                static_cast<float>(tabs_.Root().ActualWidth()), tabs_extent_,
                strip_.FlowDirection() == xaml::FlowDirection::RightToLeft)
          : 0;
  if (new_tab_position_.X() != offset) {
    new_tab_position_.X(offset);
    // Publish hit-test and host anchors after the retained button moves.
    new_tab_->control.InvalidateArrange();
  }
}

void TopChrome::SetLayout(const ShellLayout& layout,
                          const controls::Grid& sidebar) {
  const bool horizontal = layout.tabs != TabLayout::kVertical;
  if (mode_ != layout.tabs) {
    tabs_.DismissHover();
    Remove(sidebar, tabs_.Root());
    Remove(strip_, tabs_.Root());
    Remove(root_, strip_);
    Remove(header_.TabSlot(), strip_);
    Remove(strip_, new_tab_->control);
    Remove(header_.TabSlot(), new_tab_->control);
    tabs_.SetHorizontal(horizontal);
    if (horizontal) {
      controls::Grid::SetRow(tabs_.Root(), 0);
      controls::Grid::SetColumn(tabs_.Root(), 0);
      strip_.Children().Append(tabs_.Root());
      if (layout.tabs == TabLayout::kCompact) {
        header_.TabSlot().Children().Append(strip_);
      } else
        root_.Children().Append(strip_);
    } else {
      controls::Grid::SetRow(tabs_.Root(), 5);
      sidebar.Children().Append(tabs_.Root());
      root_.Children().Append(strip_);
    }
    mode_ = layout.tabs;
  }
  if (horizontal) {
    // Dynamic keeps a navigation-row fallback while its one-tab strip is
    // hidden.
    new_tab_in_strip_ =
        layout.tabs != TabLayout::kDynamic || layout.tab_strip_visible;
    const auto& parent = new_tab_in_strip_ ? strip_ : header_.TabSlot();
    Remove(new_tab_in_strip_ ? header_.TabSlot() : strip_, new_tab_->control);
    std::uint32_t index;
    if (!parent.Children().IndexOf(new_tab_->control, index))
      parent.Children().Append(new_tab_->control);
    controls::Grid::SetColumn(new_tab_->control, new_tab_in_strip_ ? 1 : 0);
    new_tab_row_height_ =
        new_tab_in_strip_ ? layout.tab_strip.height : layout.navigation.height;
  }
  const bool classic = layout.tabs == TabLayout::kClassic;
  controls::Grid::SetRow(header_.Toolbar(), classic ? 1 : 0);
  controls::Grid::SetRow(strip_, layout.tabs == TabLayout::kCompact ? 0
                                 : classic                          ? 0
                                                                    : 1);
  const float nav_height =
      horizontal ? layout.navigation.height : layout.toolbar.height;
  const float strip_height = horizontal && layout.tabs != TabLayout::kCompact
                                 ? layout.tab_strip.height
                                 : 0;
  root_.RowDefinitions().GetAt(0).Height(
      {classic ? strip_height : nav_height, xaml::GridUnitType::Pixel});
  root_.RowDefinitions().GetAt(1).Height(
      {classic ? nav_height : strip_height, xaml::GridUnitType::Pixel});
  const auto place_row = [&](const xaml::FrameworkElement& element, Rect row) {
    const xaml::Thickness margin{
        row.x - layout.toolbar.x, 0,
        std::max(0.0f, layout.toolbar.right() - row.right()), 0};
    if (element.Margin() != margin)
      element.Margin(margin);
  };
  place_row(header_.Toolbar(), horizontal ? layout.navigation : layout.toolbar);
  // Compact's strip already lives inside the inset navigation row.
  place_row(strip_, !horizontal || layout.tabs == TabLayout::kCompact
                        ? layout.toolbar
                        : layout.tab_strip);
  strip_.Visibility(layout.tab_strip_visible ? xaml::Visibility::Visible
                                             : xaml::Visibility::Collapsed);
  header_.SetLayout(layout);
  PositionNewTab();
}

void TopChrome::SetText(UiTextView text) {
  SetButtonText(*new_tab_, NativeText(UiText::kNewTab, text));
  PositionNewTab();
}

void TopChrome::AppendAnchors(std::vector<Anchor>& anchors,
                              bool visible) const {
  if (mode_ != TabLayout::kVertical)
    anchors.push_back({AnchorKind::kElement,
                       static_cast<std::uint64_t>(Element::kNewTab),
                       context_.bounds(new_tab_->control), visible});
}

void TopChrome::AppendDragExclusions(std::vector<Rect>& controls,
                                     bool visible) const {
  if (visible && mode_ != TabLayout::kVertical)
    controls.push_back(context_.bounds(new_tab_->control));
}
}  // namespace kr::win
