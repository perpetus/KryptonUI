#include "app_menu.hpp"

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

#include "krypton/ui/layout/popup_layout.hpp"
#include "krypton/ui/platform/platform.hpp"
#include "krypton/ui/presentation/menu_shortcut.hpp"
#include "native_input.hpp"

namespace kr::win {
using namespace controls;

AppMenuPresenter::AppMenuPresenter(HWND parent,
                                   ViewContext& context,
                                   Task on_closed)
    : context_(context), on_closed_(std::move(on_closed)), island_(parent) {
  context_.brushes.Install(content_);
  scroll_.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  scroll_.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroll_.Content(content_);
  // This popup already owns its scrolling, backdrop and frame. A native
  // FlyoutPresenter adds a second themed frame around the tools panel and its
  // submenus, so use the explicit token-styled surface directly.
  surface_.Child(scroll_);
  surface_.MinWidth(0);
  surface_.MinHeight(0);
  surface_.Padding(UniformThickness(kTokens.menu.padding));
  surface_.Background(context_.brushes.menu_background);
  surface_.BorderBrush(context_.brushes.menu_border);
  surface_.BorderThickness(UniformThickness(kTokens.menu.border_width));
  surface_.CornerRadius(UniformCorners(kTokens.menu.radius));
  scroll_.MinWidth(0);
  scroll_.MinHeight(0);
  backdrop_.IsHitTestVisible(false);
  layers_.Children().Append(backdrop_);
  layers_.Children().Append(surface_);
  popup_.Child(layers_);
  popup_.IsLightDismissEnabled(true);
  popup_.ShouldConstrainToRootBounds(true);
  closed_ = popup_.Closed(winrt::auto_revoke, [this](auto&&, auto&&) {
    if (open_ && opened_ && !popup_.IsOpen())
      Close();
  });
  popup_opened_ = popup_.Opened(winrt::auto_revoke, [this](auto&&, auto&&) {
    if (open_ && popup_.IsOpen()) {
      opened_ = true;
      island_.Raise();
      FocusFirst();
      fade_.Play(content_, context_.motion, context_.brushes.high_contrast());
    }
  });
  surface_sized_ = surface_.SizeChanged(
      winrt::auto_revoke, [this](auto&&, auto&&) { Reposition(); });
  key_ = content_.KeyDown(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::KeyRoutedEventArgs& args) {
        Key(args);
      });
  fallback_key_ = content_.ProcessKeyboardAccelerators(
      winrt::auto_revoke,
      [this](auto&&,
             const xaml::Input::ProcessKeyboardAcceleratorEventArgs& args) {
        if (!args.Handled() && context_.input.ForwardUnhandledKey(
                                   static_cast<std::uint32_t>(args.Key())))
          args.Handled(true);
      });
}

AppMenuPresenter::~AppMenuPresenter() {
  lifetime_.reset();
  owner_loaded_.revoke();
  owner_sized_.revoke();
  surface_sized_.revoke();
  popup_opened_.revoke();
  closed_.revoke();
  focus_loaded_.revoke();
  key_.revoke();
  fallback_key_.revoke();
  Close(false);
}

void AppMenuPresenter::Close(bool release) noexcept {
  const auto generation = ++generation_;
  const auto request = request_;
  fade_.Stop();
  const bool notify = std::exchange(open_, false);
  focus_loaded_.revoke();
  opened_ = false;
  try {
    context_.glass.Place(surface_, {});
    popup_.IsOpen(false);
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "App menu popup could not close");
  }
  island_.Hide();
  if (notify)
    try {
      context_.delegate.OnMenuClosed(request);
      if (release && generation == generation_ && request == request_)
        on_closed_();
    } catch (...) {
      platform::WriteDefaultLog(LogLevel::kWarning,
                                "App menu closure could not be reported");
    }
}

void AppMenuPresenter::Separator() {
  const auto& t = kTokens.app_menu;
  Border line;
  line.Height(t.separator_width);
  line.Background(context_.brushes.menu_border);
  line.Margin({t.separator_margin_inline, t.separator_margin_block,
               t.separator_margin_inline, t.separator_margin_block});
  content_.Children().Append(line);
}

Button AppMenuPresenter::Command(const MenuItem& item,
                                 bool compact,
                                 bool show_shortcut) {
  auto native = IconButton(
      context_.brushes, L"",
      winrt::to_hstring(item.metadata && !item.metadata->accessible_name.empty()
                            ? item.metadata->accessible_name
                            : item.label),
      [this, command = item.command_id, keep = item.keep_open,
       submenu = item.submenu, title = winrt::to_hstring(item.label)] {
        if (submenu >= 0) {
          Navigate(static_cast<std::size_t>(submenu), title);
          return;
        }
        const auto request = request_;
        context_.delegate.OnMenuCommand(request, command);
        if (!keep && open_ && request_ == request)
          Close();
      });
  const auto& t = kTokens.app_menu;
  const auto button = native->control;
  native->seen = static_cast<std::uint64_t>(item.command_id);
  if (item.submenu >= 0)
    button.Tag(winrt::box_value(item.submenu));
  button.Width(std::numeric_limits<double>::quiet_NaN());
  button.Height(std::numeric_limits<double>::quiet_NaN());
  button.MinWidth(0);
  button.MinHeight(0);
  button.Padding({compact ? t.control_padding_inline : t.row_padding_inline,
                  compact ? t.control_padding_block : t.row_padding_block,
                  compact ? t.control_padding_inline : t.row_padding_inline,
                  compact ? t.control_padding_block : t.row_padding_block});
  button.Margin(compact
                    ? UniformThickness(0)
                    : xaml::Thickness{t.row_margin_inline, t.row_margin_block,
                                      t.row_margin_inline, t.row_margin_block});
  button.CornerRadius(UniformCorners(t.row_radius));
  button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  button.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
  button.IsEnabled(item.enabled && item.type != MenuItemType::kTitle);
  button.Opacity(item.enabled ? 1 : t.disabled_opacity);
  xaml::Automation::AutomationProperties::SetAcceleratorKey(
      button, winrt::to_hstring(item.accelerator));
  if (!item.accelerator.empty()) {
    controls::ToolTipService::SetToolTip(
        button, winrt::box_value(winrt::to_hstring(item.label + " (" +
                                                   item.accelerator + ")")));
  }
  const auto make_shortcut = [&] {
    auto hint = Label(winrt::to_hstring(MenuShortcutSymbols(item.accelerator)),
                      t.shortcut_size);
    hint.FontFamily(media::FontFamily(winrt::to_hstring(t.shortcut_family)));
    hint.Foreground(context_.brushes.text);
    hint.Opacity(t.accelerator_opacity);
    hint.FlowDirection(xaml::FlowDirection::LeftToRight);
    hint.VerticalAlignment(xaml::VerticalAlignment::Center);
    xaml::Automation::AutomationProperties::SetAccessibilityView(
        hint, xaml::Automation::Peers::AccessibilityView::Raw);
    return hint;
  };
  const bool has_glyph = !item.glyph.empty();
  const auto glyph_text = winrt::to_hstring(item.glyph);
  const auto icon = [&]() -> xaml::UIElement {
    if (has_glyph) {
      auto glyph = Glyph(glyph_text, t.icon_size);
      glyph.Foreground(context_.brushes.text);
      return glyph;
    }
    if (const auto found = menu_images_.find(item.icon);
        found != menu_images_.end()) {
      controls::Image image;
      image.Width(t.icon_size);
      image.Height(t.icon_size);
      image.Source(found->second);
      return image;
    }
    return nullptr;
  }();
  const auto make_label = [&] {
    auto label = Label(winrt::to_hstring(item.label), kTokens.type.size_small);
    label.Foreground(context_.brushes.text);
    label.TextWrapping(xaml::TextWrapping::Wrap);
    return label;
  };
  if (compact) {
    button.HorizontalContentAlignment(xaml::HorizontalAlignment::Center);
    // Compact content is parented directly to the button. A XAML element
    // cannot also belong to the ordinary row's child collection.
    const xaml::UIElement value = icon ? icon : make_label();
    if (item.checked) {
      Grid selected;
      AddColumn(selected, 1, xaml::GridUnitType::Star);
      AddColumn(selected, 0, xaml::GridUnitType::Auto);
      selected.Children().Append(value);
      const auto mark = Glyph(L"\uE73E", t.icon_size);
      mark.Foreground(context_.brushes.text);
      mark.Margin({t.icon_gap, 0, 0, 0});
      Grid::SetColumn(mark, 1);
      selected.Children().Append(mark);
      button.Content(selected);
    } else {
      button.Content(value);
    }
    if (show_shortcut && !item.accelerator.empty()) {
      const auto content = button.Content().as<xaml::UIElement>();
      button.Content(nullptr);
      StackPanel keys;
      keys.Spacing(t.shortcut_stack_gap);
      keys.HorizontalAlignment(xaml::HorizontalAlignment::Center);
      content.as<xaml::FrameworkElement>().HorizontalAlignment(
          xaml::HorizontalAlignment::Center);
      keys.Children().Append(content);
      const auto hint = make_shortcut();
      hint.HorizontalAlignment(xaml::HorizontalAlignment::Center);
      keys.Children().Append(hint);
      button.Content(keys);
    }
  } else {
    Grid row;
    const bool status =
        item.metadata && !item.metadata->secondary_label.empty();
    AddColumn(row,
              status ? kTokens.site_data_menu.status_icon_size : t.icon_size,
              xaml::GridUnitType::Pixel);
    AddColumn(row, 1, xaml::GridUnitType::Star);
    AddColumn(row, 0, xaml::GridUnitType::Auto);
    if (item.checked || item.submenu >= 0)
      AddColumn(row, 0, xaml::GridUnitType::Auto);
    row.ColumnSpacing(t.icon_gap);
    if (icon) {
      if (status) {
        Border circle;
        circle.Width(kTokens.site_data_menu.status_icon_size);
        circle.Height(kTokens.site_data_menu.status_icon_size);
        circle.CornerRadius(
            UniformCorners(kTokens.site_data_menu.status_icon_size / 2));
        circle.Background(context_.brushes.pill);
        icon.as<xaml::FrameworkElement>().HorizontalAlignment(
            xaml::HorizontalAlignment::Center);
        icon.as<xaml::FrameworkElement>().VerticalAlignment(
            xaml::VerticalAlignment::Center);
        circle.Child(icon);
        row.Children().Append(circle);
        button.Padding({0, kTokens.site_data_menu.section_padding, 0,
                        kTokens.site_data_menu.section_padding});
        button.Margin(UniformThickness(0));
      } else {
        row.Children().Append(icon);
      }
    }
    const auto label = make_label();
    Grid::SetColumn(label, 1);
    if (item.metadata && !item.metadata->secondary_label.empty()) {
      StackPanel labels;
      labels.Children().Append(label);
      auto secondary = Label(winrt::to_hstring(item.metadata->secondary_label),
                             kTokens.type.size_small);
      secondary.Foreground(context_.brushes.muted);
      secondary.TextWrapping(xaml::TextWrapping::Wrap);
      labels.Children().Append(secondary);
      Grid::SetColumn(labels, 1);
      row.Children().Append(labels);
    } else {
      row.Children().Append(label);
    }
    if (!item.accelerator.empty()) {
      const auto accelerator = make_shortcut();
      Grid::SetColumn(accelerator, 2);
      row.Children().Append(accelerator);
    }
    if (item.checked) {
      const auto mark = Glyph(L"\uE73E", t.icon_size);
      mark.Foreground(context_.brushes.text);
      mark.Margin({t.icon_gap, 0, 0, 0});
      Grid::SetColumn(mark, 3);
      row.Children().Append(mark);
    }
    button.Content(row);
  }
  focus_targets_.push_back(button);
  buttons_.push_back(std::move(native));
  return button;
}

void AppMenuPresenter::Row(const MenuItem& item) {
  auto button = Command(item);
  if (item.type == MenuItemType::kSubmenu && item.submenu >= 0) {
    auto& native = *buttons_.back();
    native.click.revoke();
    native.click =
        button.Click(winrt::auto_revoke,
                     [this, index = static_cast<std::size_t>(item.submenu),
                      title = winrt::to_hstring(item.label)](auto&&, auto&&) {
                       Navigate(index, title);
                     });
    const auto grid = button.Content().as<Grid>();
    button.Tag(winrt::box_value(item.submenu));
    auto chevron =
        Glyph(content_.FlowDirection() == xaml::FlowDirection::RightToLeft
                  ? glyphs::kBack
                  : glyphs::kForward,
              kTokens.app_menu.icon_size);
    chevron.Foreground(context_.brushes.muted);
    Grid::SetColumn(chevron, 3);
    grid.Children().Append(chevron);
  }
  content_.Children().Append(button);
}

void AppMenuPresenter::Controls(const MenuItem& item) {
  if (item.label.empty() && item.submenu >= 0 &&
      static_cast<std::size_t>(item.submenu) < tree_.menus.size()) {
    Grid navigation;
    AddRow(navigation, 0, xaml::GridUnitType::Auto);
    int column = 0;
    for (const auto& child : tree_.menus[item.submenu]) {
      if (!child.IsVisible())
        continue;
      AddColumn(navigation, kTokens.site_data_menu.tile_width,
                xaml::GridUnitType::Pixel);
      const auto button = Command(child, /*compact=*/true,
                                  /*show_shortcut=*/false);
      button.Height(kTokens.site_data_menu.tile_height);
      button.Padding(UniformThickness(0));
      if (auto glyph = button.Content().try_as<TextBlock>())
        glyph.FontSize(kTokens.site_data_menu.icon_size);
      Grid::SetColumn(button, column++);
      navigation.Children().Append(button);
    }
    content_.Children().Append(navigation);
    return;
  }
  Grid row;
  const auto& t = kTokens.app_menu;
  row.Margin({t.row_margin_inline, t.row_margin_block, t.row_margin_inline,
              t.row_margin_block});
  AddColumn(row, 1, xaml::GridUnitType::Star);
  auto label = Label(winrt::to_hstring(item.label), kTokens.type.size_small);
  label.Foreground(context_.brushes.text);
  label.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
  label.Margin({t.row_padding_inline + (item.type == MenuItemType::kControls
                                            ? t.icon_size + t.icon_gap
                                            : 0),
                0, t.row_padding_inline, 0});
  std::size_t first_control = 0;
  if (item.type == MenuItemType::kControls && item.metadata &&
      !item.metadata->secondary_label.empty() && item.submenu >= 0 &&
      static_cast<std::size_t>(item.submenu) < tree_.menus.size() &&
      !tree_.menus[item.submenu].empty() &&
      tree_.menus[item.submenu].front().type == MenuItemType::kCheck) {
    auto toggle = tree_.menus[item.submenu].front();
    toggle.label = item.label;
    toggle.glyph = item.glyph;
    if (!toggle.metadata)
      toggle.metadata.emplace();
    toggle.metadata->secondary_label = item.metadata->secondary_label;
    auto button = Command(toggle);
    button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    button.Margin(UniformThickness(0));
    row.Children().Append(button);
    first_control = 1;
  } else {
    row.Children().Append(label);
  }
  if (item.submenu >= 0 &&
      static_cast<std::size_t>(item.submenu) < tree_.menus.size()) {
    int column = 1;
    for (const auto& child :
         std::span(tree_.menus[item.submenu]).subspan(first_control)) {
      if (!child.IsVisible())
        continue;
      AddColumn(row, 0, xaml::GridUnitType::Auto);
      if (child.type == MenuItemType::kSeparator) {
        Border line;
        line.Width(t.separator_width);
        line.Background(context_.brushes.menu_border);
        line.Margin({t.separator_margin_inline, t.separator_margin_block,
                     t.separator_margin_inline, t.separator_margin_block});
        Grid::SetColumn(line, column++);
        row.Children().Append(line);
      } else {
        const auto button = Command(child, true);
        if (item.type == MenuItemType::kHeader)
          button.Background(context_.brushes.pill);
        Grid::SetColumn(button, column++);
        row.Children().Append(button);
      }
    }
  }
  content_.Children().Append(row);
}

void AppMenuPresenter::Tiles(const MenuItem& item) {
  if (item.submenu < 0 ||
      static_cast<std::size_t>(item.submenu) >= tree_.menus.size())
    return;
  const auto& t = kTokens.site_data_menu;
  const auto columns = static_cast<int>(t.columns);
  Grid grid;
  grid.ColumnSpacing(t.gap);
  grid.RowSpacing(t.gap);
  grid.Margin({0, 0, 0, t.section_padding});
  for (int column = 0; column < columns; ++column)
    AddColumn(grid, t.tile_width, xaml::GridUnitType::Pixel);
  int index = 0;
  for (const auto& child : tree_.menus[item.submenu]) {
    if (!child.IsVisible())
      continue;
    if (index % columns == 0)
      AddRow(grid, t.tile_height, xaml::GridUnitType::Pixel);
    const auto button = Command(child, /*compact=*/true,
                                /*show_shortcut=*/false);
    button.Padding(UniformThickness(0));
    button.Height(t.tile_height);
    button.CornerRadius(UniformCorners(t.radius));
    button.Background(context_.brushes.pill);
    if (child.submenu >= 0 &&
        static_cast<std::size_t>(child.submenu) < tree_.menus.size()) {
      auto& native = *buttons_.back();
      const auto submenu = static_cast<std::size_t>(child.submenu);
      const auto& commands = tree_.menus[submenu];
      if (!commands.empty() &&
          commands.front().type == MenuItemType::kCommand) {
        native.click.revoke();
        native.click = button.Click(
            winrt::auto_revoke,
            [this, command = commands.front().command_id](auto&&, auto&&) {
              const auto request = request_;
              context_.delegate.OnMenuCommand(request, command);
              if (open_ && request_ == request)
                Close();
            });
      }
      native.right_tapped = button.RightTapped(
          winrt::auto_revoke,
          [this, submenu, title = winrt::to_hstring(child.label)](
              auto&&, const xaml::Input::RightTappedRoutedEventArgs& args) {
            Navigate(submenu, title);
            args.Handled(true);
          });
    }
    if (auto glyph = button.Content().try_as<TextBlock>())
      glyph.FontSize(t.icon_size);
    if (auto image = button.Content().try_as<controls::Image>()) {
      image.Width(t.icon_size);
      image.Height(t.icon_size);
    }
    // The host's accessible name and tooltip remain on the native Button even
    // though its tile shows only an icon and optional notification badge.
    if (!child.badge.empty()) {
      Grid tile;
      const auto icon = button.Content().as<xaml::UIElement>();
      button.Content(nullptr);
      tile.Children().Append(icon);
      Border badge;
      badge.MinWidth(t.badge_size);
      badge.Height(t.badge_size);
      badge.CornerRadius(UniformCorners(t.section_padding));
      badge.Background(
          context_.brushes.high_contrast()
              ? context_.brushes.text
              : context_.brushes.For(child.badge_color.argb
                                         ? child.badge_color
                                         : Color{t.badge_background}));
      badge.HorizontalAlignment(xaml::HorizontalAlignment::Right);
      badge.VerticalAlignment(xaml::VerticalAlignment::Top);
      auto text = Label(winrt::to_hstring(child.badge), t.badge_font_size);
      text.Foreground(context_.brushes.high_contrast()
                          ? context_.brushes.menu_background
                          : context_.brushes.For(Color{t.badge_foreground}));
      text.Margin({t.section_padding, 0, t.section_padding, 0});
      badge.Child(text);
      tile.Children().Append(badge);
      button.Content(tile);
    }
    Grid::SetColumn(button, index % columns);
    Grid::SetRow(button, index / columns);
    grid.Children().Append(button);
    ++index;
  }
  if (index)
    content_.Children().Append(grid);
}

void AppMenuPresenter::Section(const MenuItem& item) {
  const auto& t = kTokens.site_data_menu;
  Grid row;
  row.Margin({0, t.section_gap, 0, t.gap});
  AddColumn(row, 1, xaml::GridUnitType::Star);
  auto title = Label(winrt::to_hstring(item.label), kTokens.type.size_small);
  title.FontWeight(kSemiBold);
  title.Foreground(context_.brushes.text);
  row.Children().Append(title);
  if (item.submenu >= 0 &&
      static_cast<std::size_t>(item.submenu) < tree_.menus.size()) {
    int column = 1;
    for (const auto& child : tree_.menus[item.submenu]) {
      if (!child.IsVisible())
        continue;
      AddColumn(row, 0, xaml::GridUnitType::Auto);
      const auto button = Command(child, true);
      button.Padding({t.section_padding, 0, t.section_padding, 0});
      button.Foreground(context_.brushes.muted);
      if (auto label = button.Content().try_as<TextBlock>())
        label.Foreground(context_.brushes.muted);
      Grid::SetColumn(button, column++);
      row.Children().Append(button);
    }
  }
  content_.Children().Append(row);
}

void AppMenuPresenter::Footer(const MenuItem& item) {
  if (item.submenu < 0 ||
      static_cast<std::size_t>(item.submenu) >= tree_.menus.size())
    return;
  const auto& t = kTokens.site_data_menu;
  Border separator;
  separator.Height(kTokens.app_menu.separator_width);
  separator.Background(context_.brushes.menu_border);
  separator.Margin({0, t.section_gap, 0, t.gap});
  content_.Children().Append(separator);
  Grid row;
  row.ColumnSpacing(t.gap);
  int column = 0;
  const auto& children = tree_.menus[item.submenu];
  for (const auto& child : children) {
    if (!child.IsVisible())
      continue;
    AddColumn(
        row, column == 0 ? 1 : 0,
        column == 0 ? xaml::GridUnitType::Star : xaml::GridUnitType::Auto);
    const auto button = Command(child, column != 0);
    button.Height(t.tile_height);
    button.Padding({t.gap, 0, t.gap, 0});
    button.Margin(UniformThickness(0));
    button.CornerRadius(UniformCorners(t.radius));
    button.Background(context_.brushes.pill);
    Grid::SetColumn(button, column++);
    row.Children().Append(button);
  }
  content_.Children().Append(row);
}

void AppMenuPresenter::Build(std::size_t index) {
  fade_.Stop();
  focus_loaded_.revoke();
  content_.Children().Clear();
  focus_targets_.clear();
  buttons_.clear();
  if (path_.size() > 1) {
    MenuItem back{
        .label = winrt::to_string(back_text_),
        .glyph = content_.FlowDirection() == xaml::FlowDirection::RightToLeft
                     ? "\uE72A"
                     : "\uE72B"};
    auto button = Command(back);
    auto& native = *buttons_.back();
    native.click.revoke();
    native.click =
        button.Click(winrt::auto_revoke, [this](auto&&, auto&&) { Back(); });
    content_.Children().Append(button);
    auto title = Label(titles_.back(), kTokens.type.size);
    title.Foreground(context_.brushes.text);
    title.Margin({kTokens.app_menu.row_padding_inline,
                  kTokens.app_menu.row_padding_block,
                  kTokens.app_menu.row_padding_inline,
                  kTokens.app_menu.row_padding_block});
    content_.Children().Append(title);
    Separator();
  }
  if (index < tree_.menus.size())
    for (const auto& item : tree_.menus[index]) {
      if (!item.IsVisible())
        continue;
      if (item.type == MenuItemType::kSeparator)
        Separator();
      else if (item.type == MenuItemType::kHeader ||
               item.type == MenuItemType::kControls)
        Controls(item);
      else if (item.type == MenuItemType::kGrid)
        Tiles(item);
      else if (item.type == MenuItemType::kSection)
        Section(item);
      else if (item.type == MenuItemType::kFooter)
        Footer(item);
      else
        Row(item);
    }
}

void AppMenuPresenter::Navigate(std::size_t index,
                                winrt::hstring title) noexcept {
  try {
    if (index >= tree_.menus.size())
      return;
    path_.push_back(index);
    titles_.push_back(std::move(title));
    Build(index);
    Reposition();
    scroll_.ChangeView(
        nullptr, winrt::box_value(0.0).as<foundation::IReference<double>>(),
        nullptr, true);
    FocusFirst();
    fade_.Play(content_, context_.motion, context_.brushes.high_contrast());
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kError, "Menu subview could not open");
  }
}

void AppMenuPresenter::Back() noexcept {
  try {
    if (path_.size() < 2)
      return;
    path_.pop_back();
    titles_.pop_back();
    Build(path_.back());
    Reposition();
    FocusFirst();
    fade_.Play(content_, context_.motion, context_.brushes.high_contrast());
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kError, "Menu subview could not close");
  }
}

void AppMenuPresenter::Focus(const Button& button) {
  if (!open_ || !popup_.IsOpen())
    return;
  focus_loaded_.revoke();
  if (button.IsLoaded())
    button.Focus(xaml::FocusState::Programmatic);
  else
    focus_loaded_ = button.Loaded(
        winrt::auto_revoke,
        [this, weak = std::weak_ptr<int>(lifetime_), button](auto&&, auto&&) {
          if (!weak.expired() && open_ && popup_.IsOpen())
            button.Focus(xaml::FocusState::Programmatic);
        });
}

void AppMenuPresenter::FocusFirst() {
  const auto first = std::ranges::find_if(
      focus_targets_, [](const auto& button) { return button.IsEnabled(); });
  if (first == focus_targets_.end())
    return;
  Focus(*first);
}

void AppMenuPresenter::Key(const xaml::Input::KeyRoutedEventArgs& args) {
  using winrt::Windows::System::VirtualKey;
  if (args.Key() == VirtualKey::Escape) {
    Close();
    args.Handled(true);
    return;
  }
  const auto focused_tile =
      xaml::Input::FocusManager::GetFocusedElement(content_.XamlRoot())
          .try_as<Button>();
  const auto tile_grid =
      focused_tile
          ? media::VisualTreeHelper::GetParent(focused_tile).try_as<Grid>()
          : nullptr;
  if (tile_grid &&
      tile_grid.ColumnDefinitions().Size() ==
          static_cast<std::uint32_t>(kTokens.site_data_menu.columns) &&
      tile_grid.RowDefinitions().Size() > 0 &&
      (args.Key() == VirtualKey::Left || args.Key() == VirtualKey::Right ||
       args.Key() == VirtualKey::Up || args.Key() == VirtualKey::Down)) {
    const bool horizontal =
        args.Key() == VirtualKey::Left || args.Key() == VirtualKey::Right;
    const bool rtl =
        content_.FlowDirection() == xaml::FlowDirection::RightToLeft;
    const int step =
        horizontal ? ((args.Key() == VirtualKey::Left) != rtl ? -1 : 1)
                   : (args.Key() == VirtualKey::Up
                          ? -static_cast<int>(kTokens.site_data_menu.columns)
                          : static_cast<int>(kTokens.site_data_menu.columns));
    auto index = Grid::GetRow(focused_tile) *
                     static_cast<int>(kTokens.site_data_menu.columns) +
                 Grid::GetColumn(focused_tile) + step;
    while (index >= 0 &&
           index < static_cast<int>(tile_grid.Children().Size())) {
      const auto target = tile_grid.Children().GetAt(index).as<Button>();
      if (target.IsEnabled()) {
        target.Focus(xaml::FocusState::Keyboard);
        args.Handled(true);
        return;
      }
      index += step;
    }
    if (horizontal) {
      args.Handled(true);
      return;
    }
  }
  const bool rtl = content_.FlowDirection() == xaml::FlowDirection::RightToLeft;
  if (args.Key() == (rtl ? VirtualKey::Right : VirtualKey::Left) &&
      path_.size() > 1) {
    Back();
    args.Handled(true);
    return;
  }
  if (args.Key() == (rtl ? VirtualKey::Left : VirtualKey::Right)) {
    const auto focused =
        xaml::Input::FocusManager::GetFocusedElement(content_.XamlRoot())
            .try_as<Button>();
    if (focused && focused.Tag()) {
      const auto index = winrt::unbox_value<std::int32_t>(focused.Tag());
      const auto& items = tree_.menus[path_.back()];
      const auto entry = std::ranges::find(items, index, &MenuItem::submenu);
      Navigate(
          static_cast<std::size_t>(index),
          entry == items.end() ? back_text_ : winrt::to_hstring(entry->label));
      args.Handled(true);
    }
    return;
  }
  if (args.Key() != VirtualKey::Up && args.Key() != VirtualKey::Down)
    return;
  const auto focused =
      xaml::Input::FocusManager::GetFocusedElement(content_.XamlRoot());
  const auto found = std::ranges::find(focus_targets_, focused);
  const auto count = static_cast<std::ptrdiff_t>(focus_targets_.size());
  if (!count)
    return;
  auto index =
      found == focus_targets_.end() ? 0 : found - focus_targets_.begin();
  const auto step = args.Key() == VirtualKey::Up ? -1 : 1;
  for (std::ptrdiff_t tries = 0; tries < count; ++tries) {
    index = (index + step + count) % count;
    if (focus_targets_[index].IsEnabled()) {
      focus_targets_[index].Focus(xaml::FocusState::Keyboard);
      break;
    }
  }
  args.Handled(true);
}

bool AppMenuPresenter::RefreshInPlace(const MenuTree& tree) {
  if (tree.menus.size() != tree_.menus.size())
    return false;
  for (std::size_t menu = 0; menu < tree.menus.size(); ++menu) {
    const auto& previous = tree_.menus[menu];
    const auto& next = tree.menus[menu];
    if (previous.size() != next.size())
      return false;
    for (std::size_t index = 0; index < next.size(); ++index) {
      const auto& a = previous[index];
      const auto& b = next[index];
      const auto accessible = [](const MenuItem& item) -> std::string_view {
        return item.metadata ? item.metadata->accessible_name
                             : std::string_view{};
      };
      const auto secondary = [](const MenuItem& item) -> std::string_view {
        return item.metadata ? item.metadata->secondary_label
                             : std::string_view{};
      };
      if (a.command_id != b.command_id || a.type != b.type ||
          a.submenu != b.submenu || a.keep_open != b.keep_open ||
          a.checked != b.checked || a.IsVisible() != b.IsVisible() ||
          a.label != b.label || a.accelerator != b.accelerator ||
          a.icon != b.icon || a.glyph != b.glyph || a.badge != b.badge ||
          a.badge_color != b.badge_color || accessible(a) != accessible(b) ||
          secondary(a) != secondary(b))
        return false;
    }
  }
  // Enabled-state publications are common while a menu is open. Updating the
  // live button avoids resetting its pointer tracking and keyboard focus.
  for (std::size_t menu = 0; menu < tree.menus.size(); ++menu)
    for (std::size_t index = 0; index < tree.menus[menu].size(); ++index) {
      const auto& item = tree.menus[menu][index];
      if (item.enabled == tree_.menus[menu][index].enabled)
        continue;
      for (const auto& button : buttons_)
        if (button->seen == static_cast<std::uint64_t>(item.command_id)) {
          button->control.IsEnabled(item.enabled &&
                                    item.type != MenuItemType::kTitle);
          button->control.Opacity(
              item.enabled ? 1 : kTokens.app_menu.disabled_opacity);
        }
    }
  tree_ = tree;
  return true;
}

void AppMenuPresenter::Show(std::uint32_t request,
                            const MenuTree& tree,
                            Rect anchor,
                            const ImageStore& images,
                            Rect viewport,
                            float scale,
                            xaml::ElementTheme theme,
                            xaml::FlowDirection direction) {
  const bool refresh = open_ && request == request_;
  if (refresh && RefreshInPlace(tree)) {
    anchor_ = anchor;
    Update(viewport, scale, theme, direction);
    return;
  }
  std::optional<std::uint64_t> focused_command;
  if (refresh && popup_.IsOpen() && content_.XamlRoot()) {
    const auto focused =
        xaml::Input::FocusManager::GetFocusedElement(content_.XamlRoot());
    for (const auto& button : buttons_)
      if (button->control == focused) {
        focused_command = button->seen;
        break;
      }
  }
  if (!refresh) {
    Close(false);
    path_ = {0};
    titles_ = {L""};
  }
  request_ = request;
  tree_ = tree;
  tiled_ = std::ranges::any_of(tree_.menus.front(), [](const MenuItem& item) {
    return item.type == MenuItemType::kGrid;
  });
  const bool navigation =
      std::ranges::any_of(tree_.menus.front(), [](const MenuItem& item) {
        return item.type == MenuItemType::kControls && item.label.empty();
      });
  surface_.CornerRadius(UniformCorners(tiled_ || navigation
                                           ? kTokens.site_data_menu.panel_radius
                                           : kTokens.menu.radius));
  backdrop_.CornerRadius(surface_.CornerRadius());
  surface_.Padding(UniformThickness(tiled_ ? kTokens.site_data_menu.gap
                                           : kTokens.menu.padding));
  surface_.BorderThickness(
      UniformThickness(tiled_ ? kTokens.site_data_menu.border_width
                              : kTokens.menu.border_width));
  menu_images_.clear();
  for (const auto& menu : tree.menus)
    for (const auto& item : menu)
      if (item.icon && !menu_images_.contains(item.icon))
        if (auto image = context_.images.Get(item.icon, &images))
          menu_images_.emplace(item.icon, image);
  content_.FlowDirection(direction);
  content_.RequestedTheme(theme);
  anchor_ = anchor;
  Build(path_.back());
  open_ = true;
  EnsureOwner();
  Update(viewport, scale, theme, direction);
  if (refresh) {
    if (focused_command)
      for (const auto& button : buttons_)
        if (button->seen == *focused_command) {
          Focus(button->control);
          break;
        }
    return;
  }
  OpenWhenLoaded();
}

void AppMenuPresenter::EnsureOwner() {
  if (attached_)
    return;
  island_.Ensure();
  const auto& root = island_.Root();
  context_.brushes.Install(root);
  root.Children().Append(popup_);
  attached_ = true;
  const std::weak_ptr<int> weak = lifetime_;
  owner_loaded_ = root.Loaded(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
    if (!weak.expired())
      OpenWhenLoaded();
  });
  owner_sized_ =
      root.SizeChanged(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
        if (!weak.expired())
          OpenWhenLoaded();
      });
}

void AppMenuPresenter::Update(Rect viewport,
                              float scale,
                              xaml::ElementTheme theme,
                              xaml::FlowDirection direction) {
  if (!open_)
    return;
  if (viewport.empty()) {
    Close();
    return;
  }
  const bool changed_direction = content_.FlowDirection() != direction;
  content_.FlowDirection(direction);
  content_.RequestedTheme(theme);
  surface_.RequestedTheme(theme);
  layers_.RequestedTheme(theme);
  backdrop_.RequestedTheme(theme);
  surface_.Background(context_.brushes.menu_background);
  // Popup.SystemBackdrop does not fill constrained desktop-island popups.
  // A bounded native backdrop element renders the material behind both panels.
  backdrop_.SystemBackdrop(
      material_.For(context_.brushes.menu_backdrop_enabled(),
                    context_.brushes.menu_material(), true));
  if (changed_direction)
    Build(path_.back());
  island_.Show(viewport, scale, theme);
  OpenWhenLoaded();
}

void AppMenuPresenter::Reanchor(Rect anchor) {
  if (!open_ || anchor_ == anchor)
    return;
  anchor_ = anchor;
  Reposition();
}

void AppMenuPresenter::Reposition() noexcept {
  if (!open_ || positioning_)
    return;
  positioning_ = true;
  struct PositionScope {
    bool& active;
    ~PositionScope() { active = false; }
  } scope{positioning_};
  try {
    if (!island_.IsReady())
      return;
    const auto client = island_.ClientBounds();
    const auto available = PopupViewport(client);
    if (available.empty()) {
      Close();
      return;
    }
    const auto padding = surface_.Padding();
    const auto border = surface_.BorderThickness();
    const float inset = static_cast<float>(padding.Left + padding.Right +
                                           border.Left + border.Right);
    surface_.Width(tiled_ ? std::min<double>(kTokens.site_data_menu.panel_width,
                                             available.width)
                          : std::numeric_limits<double>::quiet_NaN());
    surface_.MaxWidth(available.width);
    surface_.MaxHeight(available.height);
    scroll_.MaxWidth(std::max(0.0f, available.width - inset));
    scroll_.MaxHeight(std::max(0.0f, available.height - inset));
    surface_.Measure({available.width, available.height});
    const auto desired = surface_.DesiredSize();
    const bool right_to_left =
        content_.FlowDirection() == xaml::FlowDirection::RightToLeft;
    const auto frame =
        tiled_ ? PlaceSiteToolsPopup(client, anchor_,
                                     {desired.Width, desired.Height},
                                     right_to_left)
               : PlaceAnchoredPopup(client, anchor_,
                                    {desired.Width, desired.Height},
                                    right_to_left);
    if (frame.empty()) {
      Close();
      return;
    }
    surface_.Background(context_.glass.For(
        surface_, GlassRole::kMenu, context_.brushes.menu_background,
        static_cast<float>(surface_.CornerRadius().TopLeft)));
    context_.glass.Place(surface_, frame);
    frame_ = frame;
    surface_.Width(frame.width);
    surface_.MaxHeight(frame.height);
    scroll_.MaxWidth(std::max(0.0f, frame.width - inset));
    scroll_.MaxHeight(std::max(0.0f, frame.height - inset));
    popup_.HorizontalOffset(frame.x - client.x);
    popup_.VerticalOffset(frame.y - client.y);
  } catch (...) {
    Close();
    platform::WriteDefaultLog(
        LogLevel::kError, "App menu could not be positioned inside the window");
  }
}

void AppMenuPresenter::OpenWhenLoaded() noexcept {
  if (!open_)
    return;
  try {
    if (!island_.IsReady())
      return;
    Reposition();
    if (open_ && !popup_.IsOpen())
      popup_.IsOpen(true);
    if (open_)
      island_.Raise();
  } catch (...) {
    Close();
    platform::WriteDefaultLog(LogLevel::kError, "App menu could not open");
  }
}
}  // namespace kr::win
