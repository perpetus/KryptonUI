#include "settings_appearance.hpp"

#include <windows.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.Foundation.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "krypton/ui/design/icon_packs.generated.hpp"
#include "krypton/ui/layout/horizontal_layout.hpp"

namespace kr::win {
winrt::Windows::Foundation::Uri LayoutImage(std::string_view file) {
  HMODULE module = nullptr;
  winrt::check_bool(
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&LayoutImage), &module));
  std::wstring path(32768, L'\0');
  const auto length =
      GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
  winrt::check_bool(length != 0 && length < path.size());
  path.resize(length);
  path.resize(path.find_last_of(L"\\/") + 1);
  path.append(L"assets/settings/");
  path.append(winrt::to_hstring(file));
  std::replace(path.begin(), path.end(), L'\\', L'/');
  return winrt::Windows::Foundation::Uri(L"file:///" + path);
}

controls::TextBlock AppearanceSettings::Text(UiText key, bool heading) {
  auto label = Label(L"", heading ? kTokens.settings.heading_size
                                  : kTokens.settings.body_size);
  label.FontFamily(
      media::FontFamily(winrt::to_hstring(kTokens.settings.font_family)));
  label.TextWrapping(xaml::TextWrapping::Wrap);
  label.Foreground(style_.text);
  if (heading)
    label.FontWeight(kSemiBold);
  if (key == UiText::kGlassHelp || key == UiText::kSettingsMenuHelp ||
      key == UiText::kSettingsCompactHelp || key == UiText::kSettingsPinHelp ||
      key == UiText::kSettingsHoverHelp) {
    label.FontSize(kTokens.settings.description_size);
    label.Foreground(style_.secondary);
    label.Margin({kTokens.settings.row_inset, kTokens.settings.row_padding,
                  kTokens.settings.row_inset, kTokens.settings.row_padding});
  }
  labels_.push_back({label, key});
  return label;
}
controls::Grid AppearanceSettings::Row(UiText key,
                                       const xaml::UIElement& control) {
  controls::Grid row;
  AddColumn(row, 1, xaml::GridUnitType::Star);
  AddColumn(row, 0, xaml::GridUnitType::Auto);
  AddRow(row, 0, xaml::GridUnitType::Auto);
  AddRow(row, 0, xaml::GridUnitType::Auto);
  row.ColumnSpacing(kTokens.settings.control_gap);
  row.Padding({kTokens.settings.row_inset, kTokens.settings.row_padding,
               kTokens.settings.row_inset, kTokens.settings.row_padding});
  row.MinHeight(kTokens.settings.row_min_height);
  row.BorderBrush(style_.border);
  if (const auto native = control.try_as<controls::Control>()) {
    native.FontSize(kTokens.settings.body_size);
    native.FontFamily(
        media::FontFamily(winrt::to_hstring(kTokens.settings.font_family)));
  }
  auto label = Text(key);
  label.VerticalAlignment(xaml::VerticalAlignment::Center);
  xaml::Automation::AutomationProperties::SetLabeledBy(control, label);
  row.Children().Append(label);
  auto layout = control.as<xaml::FrameworkElement>();
  if (const auto toggle = control.try_as<controls::ToggleSwitch>())
    layout = style_.Toggle(toggle);
  else if (const auto dropdown = control.try_as<controls::ComboBox>())
    style_.Dropdown(dropdown);
  controls::Grid::SetColumn(layout, 1);
  row.Children().Append(layout);
  SettingRow entry{.grid = row,
                   .label = label,
                   .control = layout,
                   .input = control.as<xaml::FrameworkElement>()};
  entry.resized = row.SizeChanged(winrt::auto_revoke,
                                  [this](auto&&, auto&&) { ReflowRows(); });
  rows_.push_back(std::move(entry));
  return row;
}

AppearanceSettings::AppearanceSettings(ViewContext& context,
                                       const SettingsStyle& style)
    : context_(context), style_(style) {
  root_.Spacing(kTokens.settings.section_gap);
  constexpr std::array headings{UiText::kSettingsTheme, UiText::kSettingsLayout,
                                UiText::kSettingsChrome,
                                UiText::kSettingsHover};
  for (std::size_t index = 0; index < sections_.size(); ++index) {
    sections_[index].Spacing(kTokens.settings.heading_gap);
    auto heading = Text(headings[index], true);
    heading.Margin({kTokens.settings.row_inset, 0, 0, 0});
    sections_[index].Children().Append(heading);
    if (index == 1) {
      groups_[index].Spacing(kTokens.settings.heading_gap);
      sections_[index].Children().Append(groups_[index]);
    } else {
      sections_[index].Children().Append(style_.Card(groups_[index]));
    }
    root_.Children().Append(sections_[index]);
  }
  for (auto& item : schemes_) {
    item = controls::ComboBoxItem();
    scheme_.Items().Append(item);
  }
  for (auto& item : materials_) {
    item = controls::ComboBoxItem();
    material_.Items().Append(item);
  }
  for (auto& item : glass_modes_) {
    item = controls::ComboBoxItem();
    glass_.Items().Append(item);
  }
  groups_[0].Children().Append(Row(UiText::kSettingsGlass, glass_));
  groups_[0].Children().Append(Text(UiText::kGlassHelp));
  glass_changed_ =
      glass_.SelectionChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
        if (!reconciling_ && glass_.SelectedIndex() >= 0)
          context_.delegate.OnAppearanceEdited(
              {AppearanceOption::kGlassMode,
               static_cast<std::uint32_t>(glass_.SelectedIndex())});
      });
  groups_[0].Children().Append(Row(UiText::kSettingsColorScheme, scheme_));
  groups_[0].Children().Append(Row(UiText::kSettingsMenuMaterial, material_));
  // Pack names are product names and stay untranslated.
  for (std::size_t index = 0; index < icon_packs_.size(); ++index) {
    icon_packs_[index] = controls::ComboBoxItem();
    icon_packs_[index].Content(winrt::box_value(
        winrt::to_hstring(design::icon_packs::kPackNames[index])));
    icon_pack_.Items().Append(icon_packs_[index]);
  }
  groups_[0].Children().Append(Row(UiText::kSettingsIcons, icon_pack_));
  icon_pack_changed_ =
      icon_pack_.SelectionChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
        if (!reconciling_ && icon_pack_.SelectedIndex() >= 0)
          context_.delegate.OnAppearanceEdited(
              {AppearanceOption::kIconPack,
               static_cast<std::uint32_t>(icon_pack_.SelectedIndex())});
      });
  scheme_changed_ =
      scheme_.SelectionChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
        if (!reconciling_ && scheme_.SelectedIndex() >= 0)
          context_.delegate.OnAppearanceEdited(
              {AppearanceOption::kColorScheme,
               static_cast<std::uint32_t>(scheme_.SelectedIndex())});
      });
  material_changed_ =
      material_.SelectionChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
        if (!reconciling_ && material_.SelectedIndex() >= 0)
          context_.delegate.OnAppearanceEdited(
              {AppearanceOption::kMenuMaterial,
               static_cast<std::uint32_t>(material_.SelectedIndex())});
      });
  for (auto& item : search_bar_choices_) {
    item = controls::ComboBoxItem();
    search_bar_.Items().Append(item);
  }
  groups_[0].Children().Append(
      Row(UiText::kSettingsSearchBarBehavior, search_bar_));
  search_bar_changed_ =
      search_bar_.SelectionChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
        if (!reconciling_ && search_bar_.SelectedIndex() >= 0)
          context_.delegate.OnAppearanceEdited(
              {AppearanceOption::kSearchBarBehavior,
               static_cast<std::uint32_t>(search_bar_.SelectedIndex())});
      });
  search_bar_menu_backdrop_.OnContent(winrt::box_value(L""));
  search_bar_menu_backdrop_.OffContent(winrt::box_value(L""));
  search_bar_menu_backdrop_.VerticalAlignment(xaml::VerticalAlignment::Center);
  groups_[0].Children().Append(
      Row(UiText::kSettingsSearchBarMenuBackdrop, search_bar_menu_backdrop_));
  search_bar_backdrop_changed_ = search_bar_menu_backdrop_.Toggled(
      winrt::auto_revoke, [this](auto&&, auto&&) {
        if (!reconciling_)
          context_.delegate.OnAppearanceEdited(
              {AppearanceOption::kSearchBarMenuBackdrop,
               search_bar_menu_backdrop_.IsOn() ? 1u : 0u});
      });
  for (auto& item : new_tab_choices_) {
    item = controls::ComboBoxItem();
    new_tab_.Items().Append(item);
  }
  groups_[0].Children().Append(Row(UiText::kSettingsNewTabBehavior, new_tab_));
  new_tab_changed_ =
      new_tab_.SelectionChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
        if (!reconciling_ && new_tab_.SelectedIndex() >= 0)
          context_.delegate.OnAppearanceEdited(
              {AppearanceOption::kNewTabBehavior,
               static_cast<std::uint32_t>(new_tab_.SelectedIndex())});
      });
  groups_[0].Children().Append(Text(UiText::kSettingsMenuHelp));
  groups_[1].Children().Append(Text(UiText::kHorizontalTabs));
  groups_[1].Children().Append(horizontal_);
  groups_[1].Children().Append(Text(UiText::kVerticalTabs));
  groups_[1].Children().Append(vertical_);
  tiles_.reserve(SettingsLayouts().size());
  BuildLayouts(true, horizontal_);
  BuildLayouts(false, vertical_);
  horizontal_sized_ = horizontal_.SizeChanged(
      winrt::auto_revoke, [this](auto&&, auto&&) { Reflow(); });
  vertical_sized_ = vertical_.SizeChanged(winrt::auto_revoke,
                                          [this](auto&&, auto&&) { Reflow(); });
  groups_[2].Children().Append(Text(UiText::kSettingsCompactHelp));
  groups_[2].Children().Append(Text(UiText::kSettingsPinHelp));
  for (const auto& option : SettingsLayoutOptions()) {
    ToggleRow entry;
    entry.option = option.option;
    entry.control = controls::ToggleSwitch();
    entry.control.OnContent(winrt::box_value(L""));
    entry.control.OffContent(winrt::box_value(L""));
    entry.control.VerticalAlignment(xaml::VerticalAlignment::Center);
    entry.row = Row(option.label, entry.control);
    entry.changed = entry.control.Toggled(
        winrt::auto_revoke,
        [this, kind = option.option](auto&& sender, auto&&) {
          if (!reconciling_)
            context_.delegate.OnSettingsLayoutEdited(
                {kind, sender.template as<controls::ToggleSwitch>().IsOn()});
        });
    groups_[2].Children().Append(entry.row);
    toggles_.push_back(std::move(entry));
  }
  sidebar_width_.SpinButtonPlacementMode(
      controls::NumberBoxSpinButtonPlacementMode::Compact);
  sidebar_width_row_ = Row(UiText::kSettingsSidebarWidth, sidebar_width_);
  groups_[2].Children().Append(sidebar_width_row_);
  width_changed_ =
      sidebar_width_.ValueChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
        if (!reconciling_ && std::isfinite(sidebar_width_.Value()))
          context_.delegate.OnSidebarResized(
              static_cast<float>(sidebar_width_.Value()));
      });
  groups_[3].Children().Append(Text(UiText::kSettingsHoverHelp));
  for (const auto& item :
       {std::pair{hover_images_, UiText::kSettingsHoverImages},
        std::pair{hover_memory_, UiText::kSettingsHoverMemory}}) {
    item.first.OnContent(winrt::box_value(L""));
    item.first.OffContent(winrt::box_value(L""));
    groups_[3].Children().Append(Row(item.second, item.first));
  }
  images_changed_ =
      hover_images_.Toggled(winrt::auto_revoke, [this](auto&&, auto&&) {
        if (!reconciling_)
          context_.delegate.OnAppearanceEdited(
              {AppearanceOption::kHoverImages, hover_images_.IsOn() ? 1u : 0u});
      });
  memory_changed_ =
      hover_memory_.Toggled(winrt::auto_revoke, [this](auto&&, auto&&) {
        if (!reconciling_)
          context_.delegate.OnAppearanceEdited(
              {AppearanceOption::kHoverMemory, hover_memory_.IsOn() ? 1u : 0u});
      });
}

void AppearanceSettings::BuildLayouts(bool horizontal, controls::Grid group) {
  group.ColumnSpacing(kTokens.spacing.tab_padding);
  group.RowSpacing(kTokens.spacing.tab_padding);
  AddColumn(group, 1, xaml::GridUnitType::Star);
  std::size_t row = 0;
  for (const auto& choice : SettingsLayouts()) {
    if (choice.horizontal != horizontal)
      continue;
    LayoutTile tile;
    tile.choice = choice.choice;
    tile.control = controls::RadioButton();
    tile.control.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    tile.control.GroupName(L"krypton-browser-layout");
    tile.control.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
    tile.frame = controls::Border();
    tile.frame.CornerRadius(UniformCorners(kTokens.radius.tab));
    tile.frame.Padding(UniformThickness(kTokens.spacing.tab_padding));
    tile.frame.BorderThickness(UniformThickness(kTokens.menu.border_width));
    controls::StackPanel content;
    content.Spacing(kTokens.spacing.tab_icon_gap);
    tile.image = controls::Image();
    tile.image.Stretch(media::Stretch::Uniform);
    tile.image.Width(0);
    tile.bitmap = media::Imaging::BitmapImage();
    // URI decoding and file loading are asynchronous WinUI services.
    tile.bitmap.UriSource(LayoutImage(choice.image));
    tile.image.Source(tile.bitmap);
    content.Children().Append(tile.image);
    tile.label = Text(choice.label);
    content.Children().Append(tile.label);
    tile.frame.Child(content);
    tile.control.Content(tile.frame);
    AddRow(group, 0, xaml::GridUnitType::Auto);
    controls::Grid::SetRow(tile.control, static_cast<int>(row++));
    tile.checked = tile.control.Checked(
        winrt::auto_revoke, [this, selected = choice.choice](auto&&, auto&&) {
          if (!reconciling_)
            context_.delegate.OnSettingsLayoutSelected(selected);
        });
    group.Children().Append(tile.control);
    tiles_.push_back(std::move(tile));
  }
}

void AppearanceSettings::Reflow() {
  if (reflowing_)
    return;
  reflowing_ = true;
  struct Reset {
    bool& value;
    ~Reset() { value = false; }
  } reset{reflowing_};
  for (const auto& group : {horizontal_, vertical_}) {
    const float available = static_cast<float>(group.ActualWidth());
    if (available <= 0)
      continue;
    float minimum = 0;
    for (auto& tile : tiles_) {
      if (tile.control.Parent() != group)
        continue;
      tile.image.Width(0);
      tile.label.MaxWidth(std::numeric_limits<double>::infinity());
      tile.control.Measure({std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::infinity()});
      minimum = std::max(minimum, tile.control.DesiredSize().Width);
    }
    const auto columns = SettingsCardColumns(
        available + kTokens.spacing.tab_padding,
        minimum + kTokens.spacing.tab_padding, group.Children().Size());
    const auto row_count = (group.Children().Size() + columns - 1) / columns;
    if (group.ColumnDefinitions().Size() != columns) {
      group.ColumnDefinitions().Clear();
      for (std::size_t index = 0; index < columns; ++index)
        AddColumn(group, 1, xaml::GridUnitType::Star);
    }
    if (group.RowDefinitions().Size() != row_count) {
      group.RowDefinitions().Clear();
      for (std::size_t index = 0; index < row_count; ++index)
        AddRow(group, 0, xaml::GridUnitType::Auto);
    }
    const auto width =
        SettingsCardWidth(available, kTokens.spacing.tab_padding, columns);
    std::size_t index = 0;
    for (auto& tile : tiles_) {
      if (tile.control.Parent() != group)
        continue;
      tile.label.Measure({std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::infinity()});
      const float native_overhead =
          tile.control.DesiredSize().Width - tile.label.DesiredSize().Width;
      const auto image_width = std::max(0.0f, width - native_overhead);
      tile.image.Width(image_width);
      tile.label.MaxWidth(image_width);
      controls::Grid::SetColumn(tile.control,
                                static_cast<int>(index % columns));
      controls::Grid::SetRow(tile.control, static_cast<int>(index / columns));
      ++index;
    }
  }
}

void AppearanceSettings::ReflowRows() {
  if (reflowing_rows_)
    return;
  reflowing_rows_ = true;
  struct Reset {
    bool& value;
    ~Reset() { value = false; }
  } reset{reflowing_rows_};
  const auto infinity = std::numeric_limits<float>::infinity();
  for (auto& entry : rows_) {
    const auto padding = entry.grid.Padding();
    const auto available = static_cast<float>(entry.grid.ActualWidth() -
                                              padding.Left - padding.Right);
    if (available <= 0)
      continue;
    entry.label.Measure({infinity, infinity});
    entry.control.MaxWidth(std::numeric_limits<double>::infinity());
    entry.control.Measure({infinity, infinity});
    const bool stacked =
        !SettingsRowFits(available, entry.label.DesiredSize().Width,
                         entry.control.DesiredSize().Width,
                         static_cast<float>(entry.grid.ColumnSpacing()));
    entry.control.MaxWidth(available);
    if (entry.stacked == stacked)
      continue;
    entry.stacked = stacked;
    controls::Grid::SetRow(entry.control, stacked ? 1 : 0);
    controls::Grid::SetColumn(entry.control, stacked ? 0 : 1);
    controls::Grid::SetColumnSpan(entry.control, stacked ? 2 : 1);
    controls::Grid::SetColumnSpan(entry.label, stacked ? 2 : 1);
    entry.grid.RowSpacing(stacked ? kTokens.spacing.tab_icon_gap : 0);
  }
}

void AppearanceSettings::Render(const ShellViewModel::SettingsVm& vm,
                                UiTextView text,
                                const ThemeVm& theme,
                                bool refresh_text) {
  reconciling_ = true;
  struct Reset {
    bool& value;
    ~Reset() { value = false; }
  } reset{reconciling_};
  for (std::size_t index = 0; index < sections_.size(); ++index) {
    const bool visible = index == 0 || index == 3 ? vm.state.appearance_editable
                                                  : vm.state.layout_editable;
    sections_[index].Visibility(visible ? xaml::Visibility::Visible
                                        : xaml::Visibility::Collapsed);
  }
  const bool text_changed = !text_ready_ || refresh_text;
  if (text_changed) {
    for (const auto& entry : labels_) {
      const auto value = NativeText(entry.key, text);
      if (entry.label.Text() != value)
        entry.label.Text(value);
    }
    const auto schemes = SettingsColorSchemes(),
               materials = SettingsMenuMaterials();
    for (std::size_t i = 0; i < schemes.size(); ++i)
      schemes_[i].Content(winrt::box_value(NativeText(schemes[i], text)));
    for (std::size_t i = 0; i < materials.size(); ++i)
      materials_[i].Content(winrt::box_value(NativeText(materials[i], text)));
    text_ready_ = true;
  }
  for (const auto& entry : labels_) {
    std::optional<bool> visible;
    switch (entry.key) {
      case UiText::kSettingsTheme:
      case UiText::kSettingsMenuHelp:
      case UiText::kSettingsSearchBarBehavior:
      case UiText::kSettingsSearchBarMenuBackdrop:
      case UiText::kSettingsNewTabBehavior:
      case UiText::kSettingsHover:
      case UiText::kSettingsHoverHelp:
        visible = vm.state.appearance_editable;
        break;
      case UiText::kSettingsLayout:
      case UiText::kHorizontalTabs:
      case UiText::kVerticalTabs:
      case UiText::kSettingsChrome:
      case UiText::kSettingsCompactHelp:
      case UiText::kSettingsPinHelp:
        visible = vm.state.layout_editable;
        break;
      default:
        break;
    }
    if (visible)
      entry.label.Visibility(*visible ? xaml::Visibility::Visible
                                      : xaml::Visibility::Collapsed);
  }
  constexpr std::array glass_labels{
      UiText::kGlassOff, UiText::kGlassPerformance, UiText::kGlassQuality};
  for (std::size_t i = 0; i < glass_modes_.size(); ++i)
    glass_modes_[i].Content(
        winrt::box_value(NativeText(glass_labels[i], text)));
  glass_.IsEnabled(vm.state.appearance_editable);
  if (glass_.SelectedIndex() != static_cast<int>(vm.appearance.glass_mode))
    glass_.SelectedIndex(static_cast<int>(vm.appearance.glass_mode));
  Accessible(glass_, NativeText(UiText::kSettingsGlass, text));
  constexpr std::array search_bar_labels{UiText::kSearchBarNormal,
                                         UiText::kSearchBarFloatingOnType,
                                         UiText::kSearchBarAlwaysFloating};
  for (std::size_t i = 0; i < search_bar_labels.size(); ++i)
    search_bar_choices_[i].Content(
        winrt::box_value(NativeText(search_bar_labels[i], text)));
  search_bar_.IsEnabled(vm.state.appearance_editable);
  const auto search_behavior =
      static_cast<int>(SelectedSearchBarBehavior(vm.appearance));
  if (search_bar_.SelectedIndex() != search_behavior)
    search_bar_.SelectedIndex(search_behavior);
  Accessible(search_bar_, NativeText(UiText::kSettingsSearchBarBehavior, text));
  search_bar_menu_backdrop_.IsEnabled(vm.state.appearance_editable);
  search_bar_menu_backdrop_.IsOn(
      vm.appearance.flags.Has(AppearanceFlag::kSearchBarMenuBackdrop));
  Accessible(search_bar_menu_backdrop_,
             NativeText(UiText::kSettingsSearchBarMenuBackdrop, text));
  constexpr std::array new_tab_labels{UiText::kNewTabOpensSearchBar,
                                      UiText::kNewTabOpensPage};
  for (std::size_t i = 0; i < new_tab_labels.size(); ++i)
    new_tab_choices_[i].Content(
        winrt::box_value(NativeText(new_tab_labels[i], text)));
  new_tab_.IsEnabled(vm.state.appearance_editable);
  const auto new_tab =
      static_cast<int>(vm.appearance.flags.Has(AppearanceFlag::kNewTabPage)
                           ? NewTabBehavior::kNewTabPage
                           : NewTabBehavior::kSearchBar);
  if (new_tab_.SelectedIndex() != new_tab)
    new_tab_.SelectedIndex(new_tab);
  Accessible(new_tab_, NativeText(UiText::kSettingsNewTabBehavior, text));
  scheme_.IsEnabled(vm.state.appearance_editable);
  material_.IsEnabled(vm.state.appearance_editable);
  icon_pack_.IsEnabled(vm.state.icon_pack_editable);
  if (icon_pack_.SelectedIndex() != static_cast<int>(vm.appearance.icon_pack))
    icon_pack_.SelectedIndex(static_cast<int>(vm.appearance.icon_pack));
  Accessible(icon_pack_, NativeText(UiText::kSettingsIcons, text));
  if (scheme_.SelectedIndex() != static_cast<int>(vm.appearance.scheme))
    scheme_.SelectedIndex(static_cast<int>(vm.appearance.scheme));
  if (material_.SelectedIndex() !=
      static_cast<int>(vm.appearance.menu_material))
    material_.SelectedIndex(static_cast<int>(vm.appearance.menu_material));
  Accessible(scheme_, NativeText(UiText::kSettingsColorScheme, text));
  Accessible(material_, NativeText(UiText::kSettingsMenuMaterial, text));
  for (auto& tile : tiles_) {
    const bool selected = tile.choice == vm.selected_layout;
    tile.control.IsEnabled(vm.state.layout_editable);
    const auto checked = tile.control.IsChecked();
    if (!checked || checked.Value() != selected)
      tile.control.IsChecked(selected);
    tile.frame.BorderBrush(selected ? context_.brushes.For(theme.accent)
                                    : context_.brushes.menu_border);
    Accessible(tile.control, tile.label.Text());
  }
  const bool vertical = TabLayoutOf(vm.layout.flags) == TabLayout::kVertical;
  for (auto& row : toggles_) {
    const bool available = LayoutOptionAvailable(vm.layout, row.option);
    row.row.Visibility(available ? xaml::Visibility::Visible
                                 : xaml::Visibility::Collapsed);
    row.control.IsEnabled(vm.state.layout_editable);
    row.control.IsOn(LayoutOptionValue(vm.layout, row.option));
  }
  sidebar_width_row_.Visibility(vertical ? xaml::Visibility::Visible
                                         : xaml::Visibility::Collapsed);
  sidebar_width_.IsEnabled(vm.state.layout_editable);
  sidebar_width_.Minimum(vm.minimum_sidebar_width);
  sidebar_width_.Maximum(kTokens.sidebar.max_width);
  sidebar_width_.Value(vm.layout.sidebar_width > 0
                           ? vm.layout.sidebar_width
                           : kTokens.sidebar.default_width);
  Accessible(sidebar_width_, NativeText(UiText::kSettingsSidebarWidth, text));
  hover_images_.IsEnabled(vm.state.appearance_editable);
  hover_memory_.IsEnabled(vm.state.appearance_editable);
  hover_images_.IsOn(
      !vm.appearance.flags.Has(AppearanceFlag::kHideHoverImages));
  hover_memory_.IsOn(
      !vm.appearance.flags.Has(AppearanceFlag::kHideHoverMemory));
  Accessible(hover_images_, NativeText(UiText::kSettingsHoverImages, text));
  Accessible(hover_memory_, NativeText(UiText::kSettingsHoverMemory, text));
  for (std::size_t index = 0; index < groups_.size(); ++index) {
    if (index == 1)
      continue;  // Layout tiles already own their surfaces.
    bool first = true;
    for (const auto& child : groups_[index].Children()) {
      if (const auto row = child.try_as<controls::Grid>();
          row && row.Visibility() == xaml::Visibility::Visible) {
        const xaml::Thickness divider{
            0, first ? 0 : kTokens.settings.border_width, 0, 0};
        if (row.BorderThickness() != divider)
          row.BorderThickness(divider);
        first = false;
      }
    }
  }
  if (text_changed) {
    Reflow();
    ReflowRows();
  }
}
bool AppearanceSettings::Focus(UiText key) {
  if (key == UiText::kSettingsLayout && !tiles_.empty()) {
    tiles_.front().control.StartBringIntoView();
    return tiles_.front().control.Focus(xaml::FocusState::Programmatic);
  }
  for (const auto& label : labels_)
    if (label.key == key)
      for (const auto& row : rows_)
        if (row.label == label.label) {
          row.grid.StartBringIntoView();
          return row.input.Focus(xaml::FocusState::Programmatic);
        }
  return false;
}
}  // namespace kr::win
