#include "browser_tools_window.hpp"

#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Windows.System.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "browser_tools_icons.hpp"
#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
using namespace controls;
namespace {
void Text(const TextBlock& control, const winrt::hstring& value) {
  if (control.Text() != value)
    control.Text(value);
}
void TaskColumns(const Grid& grid) {
  const auto& t = kTokens.browser_tools;
  AddColumn(grid, 1, xaml::GridUnitType::Star).MinWidth(t.name_min_width);
  for (const auto width :
       {t.memory_width, t.cpu_width, t.network_width, t.process_width})
    AddColumn(grid, width, xaml::GridUnitType::Pixel);
}
}  // namespace
BrowserToolWindow::BrowserToolWindow(HWND owner,
                                     ViewContext& context,
                                     bool tasks)
    : owner_(owner), context_(context), tasks_(tasks) {}
BrowserToolWindow::~BrowserToolWindow() {
  input_target_.Reset();
  lifetime_.reset();
  searched_.revoke();
  filtered_.revoke();
  selected_event_.revoke();
  realized_.revoke();
  activated_.revoke();
  key_.revoke();
  rows_.clear();
  try {
    if (closing_registered_)
      app_window_.Closing(closing_);
    if (window_)
      window_.Close();
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Browser tool window could not close");
  }
}
void BrowserToolWindow::Build() {
  if (window_)
    return;
  const auto& t = kTokens.browser_tools;
  if (!tasks_)
    for (std::size_t i = 0; i < setting_labels_.size(); ++i)
      setting_labels_[i] = winrt::to_hstring(kPermissionSettingLabels[i]);
  window_ = xaml::Window();
  window_.Title(
      winrt::to_hstring(tasks_ ? kTaskManagerTitle : kPermissionManagerTitle));
  window_.SystemBackdrop(media::MicaBackdrop());
  app_window_ = window_.AppWindow();
  app_window_.TitleBar().IconShowOptions(
      winrt::Microsoft::UI::Windowing::IconShowOptions::HideIconAndSystemMenu);
  HWND native = nullptr;
  winrt::check_hresult(
      window_.as<::IWindowNative>()->get_WindowHandle(&native));
  winrt::check_bool(input_target_.Initialize(native).has_value());
  SetLastError(0);
  if (!SetWindowLongPtrW(native, GWLP_HWNDPARENT,
                         reinterpret_cast<LONG_PTR>(owner_)))
    winrt::check_bool(GetLastError() == 0);
  const float scale =
      static_cast<float>(GetDpiForWindow(owner_)) / USER_DEFAULT_SCREEN_DPI;
  app_window_.Resize({static_cast<int>(std::lround(t.width * scale)),
                      static_cast<int>(std::lround(t.height * scale))});
  if (const auto presenter =
          app_window_.Presenter()
              .try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>()) {
    presenter.PreferredMinimumWidth(
        winrt::box_value(static_cast<int>(std::lround(t.minimum_width * scale)))
            .as<foundation::IReference<int>>());
    presenter.PreferredMinimumHeight(
        winrt::box_value(
            static_cast<int>(std::lround(t.minimum_height * scale)))
            .as<foundation::IReference<int>>());
  }
  root_ = Grid();
  root_.Padding(UniformThickness(t.padding));
  root_.RowSpacing(t.gap);
  context_.brushes.Install(root_);
  for (int row = 0; row < 5; ++row)
    AddRow(root_, row == 3 ? 1 : 0,
           row == 3 ? xaml::GridUnitType::Star : xaml::GridUnitType::Auto);
  StackPanel heading;
  title_ = Label(
      winrt::to_hstring(tasks_ ? kTaskManagerTitle : kPermissionManagerTitle),
      t.title_size);
  title_.FontWeight(kSemiBold);
  subtitle_ = Label(L"", kTokens.type.size_small);
  subtitle_.TextWrapping(xaml::TextWrapping::Wrap);
  heading.Children().Append(title_);
  heading.Children().Append(subtitle_);
  root_.Children().Append(heading);
  Grid search_row;
  search_row.ColumnSpacing(t.gap);
  AddColumn(search_row, 1, xaml::GridUnitType::Star);
  search_ = TextBox();
  const auto search_label = winrt::to_hstring(
      tasks_ ? kTaskSearchPlaceholder : kPermissionSearchPlaceholder);
  search_.PlaceholderText(search_label);
  Accessible(search_, search_label);
  search_row.Children().Append(search_);
  if (tasks_) {
    AddColumn(search_row, 0, xaml::GridUnitType::Auto);
    filter_ = ComboBox();
    for (const auto label : kTaskFilterLabels)
      filter_.Items().Append(winrt::box_value(winrt::to_hstring(label)));
    Accessible(filter_, winrt::to_hstring(kTaskCategoryLabel));
    Grid::SetColumn(filter_, 1);
    search_row.Children().Append(filter_);
  }
  Grid::SetRow(search_row, 1);
  root_.Children().Append(search_row);
  Grid column_headers;
  column_headers.Margin({kTokens.app_menu.row_padding_inline, 0,
                         kTokens.app_menu.row_padding_inline, 0});
  if (tasks_) {
    TaskColumns(column_headers);
    for (std::size_t column = 0; column < headers_.size(); ++column) {
      headers_[column] = IconButton(
          context_.brushes, L"", winrt::to_hstring(kTaskColumnLabels[column]),
          [this, column] {
            context_.delegate.OnTaskSortChanged(
                static_cast<TaskColumn>(column));
          });
      auto button = headers_[column]->control;
      button.Width(std::numeric_limits<double>::quiet_NaN());
      button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
      button.HorizontalContentAlignment(column
                                            ? xaml::HorizontalAlignment::Right
                                            : xaml::HorizontalAlignment::Left);
      Grid::SetColumn(button, static_cast<int>(column));
      column_headers.Children().Append(button);
    }
  }
  Grid::SetRow(column_headers, 2);
  root_.Children().Append(column_headers);
  Grid collection;
  list_ = ListView();
  list_.SelectionMode(tasks_ ? ListViewSelectionMode::Multiple
                             : ListViewSelectionMode::None);
  list_.IsItemClickEnabled(false);
  Accessible(list_, winrt::to_hstring(tasks_ ? kBrowserTasksLabel
                                             : kPermissionManagerTitle));
  items_ = winrt::single_threaded_observable_vector<IInspectable>();
  list_.ItemsSource(items_);
  list_.ItemTemplate(xaml::Markup::XamlReader::Load(tasks_ ? LR"(
    <DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
      <Grid><Grid><Image/><TextBlock/><TextBlock Grid.Column="1"/></Grid><TextBlock Grid.Column="1"/><TextBlock Grid.Column="2"/><TextBlock Grid.Column="3"/><TextBlock Grid.Column="4"/></Grid>
    </DataTemplate>)"
                                                           : LR"(
    <DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
      <Grid><TextBlock/><StackPanel Grid.Column="1"><TextBlock/><TextBlock/><TextBlock/></StackPanel><ComboBox Grid.Column="2"/></Grid>
    </DataTemplate>)")
                         .as<xaml::DataTemplate>());
  collection.Children().Append(list_);
  empty_ =
      Label(winrt::to_hstring(tasks_ ? kNoTasksLabel : kNoPermissionsLabel),
            kTokens.type.size);
  empty_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  empty_.VerticalAlignment(xaml::VerticalAlignment::Center);
  collection.Children().Append(empty_);
  Grid::SetRow(collection, 3);
  root_.Children().Append(collection);
  StackPanel footer;
  footer.Spacing(t.gap);
  error_ = InfoBar();
  error_.Severity(InfoBarSeverity::Error);
  error_.IsOpen(false);
  footer.Children().Append(error_);
  Grid bottom;
  AddColumn(bottom, 1, xaml::GridUnitType::Star);
  AddColumn(bottom, 0, xaml::GridUnitType::Auto);
  status_ = Label(L"", kTokens.type.size_small);
  bottom.Children().Append(status_);
  action_button_ = IconButton(
      context_.brushes, L"",
      winrt::to_hstring(tasks_ ? kEndProcessLabel : kResetPermissionsLabel),
      [this] {
        if (tasks_)
          context_.delegate.OnEndTasks();
        else
          context_.delegate.OnSitePermissionsReset();
      });
  action_ = action_button_->control;
  action_.Width(std::numeric_limits<double>::quiet_NaN());
  action_.Height(std::numeric_limits<double>::quiet_NaN());
  action_.Padding({t.gap, kTokens.app_menu.row_padding_block, t.gap,
                   kTokens.app_menu.row_padding_block});
  action_.Content(winrt::box_value(
      winrt::to_hstring(tasks_ ? kEndProcessLabel : kResetPermissionsLabel)));
  Grid::SetColumn(action_, 1);
  bottom.Children().Append(action_);
  footer.Children().Append(bottom);
  Grid::SetRow(footer, 4);
  root_.Children().Append(footer);
  const std::weak_ptr<int> weak = lifetime_;
  searched_ =
      search_.TextChanged(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
        if (weak.expired() || binding_)
          return;
        const auto text = winrt::to_string(search_.Text());
        if (tasks_)
          context_.delegate.OnTaskSearchChanged(text);
        else
          context_.delegate.OnPermissionSearchChanged(text);
      });
  if (tasks_) {
    filtered_ = filter_.SelectionChanged(
        winrt::auto_revoke, [this, weak](auto&&, auto&&) {
          if (!weak.expired() && !binding_ && filter_.SelectedIndex() >= 0)
            context_.delegate.OnTaskFilterChanged(
                static_cast<TaskFilter>(filter_.SelectedIndex()));
        });
    selected_event_ = list_.SelectionChanged(winrt::auto_revoke,
                                             [this, weak](auto&&, auto&&) {
                                               if (!weak.expired() && !binding_)
                                                 UpdateSelection();
                                             });
    activated_ =
        list_.DoubleTapped(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
          if (!weak.expired() && list_.SelectedItem())
            context_.delegate.OnTaskActivated(TaskId{
                winrt::unbox_value<std::uint64_t>(list_.SelectedItem())});
        });
  }
  realized_ = list_.ContainerContentChanging(winrt::auto_revoke,
                                             [this, weak](auto&&, auto&& args) {
                                               if (!weak.expired())
                                                 Realize(args);
                                             });
  key_ = root_.KeyDown(
      winrt::auto_revoke,
      [this, weak](auto&&, const xaml::Input::KeyRoutedEventArgs& args) {
        if (!weak.expired() &&
            args.Key() == winrt::Windows::System::VirtualKey::Escape) {
          Dismiss();
          args.Handled(true);
        }
      });
  closing_ = app_window_.Closing([this, weak](auto&&, auto&& args) {
    if (!weak.expired()) {
      args.Cancel(true);
      Dismiss();
    }
  });
  closing_registered_ = true;
  window_.Content(root_);
}
void BrowserToolWindow::Dismiss() {
  if (!visible_)
    return;
  visible_ = false;
  const auto window = app_window_;
  hide_.Hide(input_target_.Window(), root_.DispatcherQueue(),
             [window] { window.Hide(); });
  ReleaseImages();
  if (tasks_)
    context_.delegate.OnTaskManagerDismissed();
  else
    context_.delegate.OnSitePermissionsDismissed();
}
void BrowserToolWindow::ReleaseImages() {
  for (auto& [id, record] : records_)
    record.image = nullptr;
  for (auto& [key, row] : rows_)
    if (row->image)
      row->image.Source(nullptr);
}
void BrowserToolWindow::UpdateSelection() {
  selected_.clear();
  for (const auto& item : list_.SelectedItems())
    selected_.push_back(TaskId{winrt::unbox_value<std::uint64_t>(item)});
  context_.delegate.OnTasksSelected(selected_);
}
void BrowserToolWindow::Realize(const ContainerContentChangingEventArgs& args) {
  const auto container = args.ItemContainer().as<ListViewItem>();
  const auto key = winrt::get_abi(container);
  if (args.InRecycleQueue()) {
    if (const auto found = rows_.find(key); found != rows_.end()) {
      if (auto record = records_.find(found->second->id);
          record != records_.end()) {
        record->second.row = nullptr;
        record->second.image = nullptr;
      }
      found->second->id = 0;
      if (found->second->image)
        found->second->image.Source(nullptr);
    }
    return;
  }
  const auto grid = container.ContentTemplateRoot().try_as<Grid>();
  if (!grid) {
    const std::weak_ptr<int> weak = lifetime_;
    if (args.Phase() < 2)
      args.RegisterUpdateCallback(args.Phase() + 1,
                                  [this, weak](auto&&, auto&& next) {
                                    if (!weak.expired())
                                      Realize(next);
                                  });
    return;
  }
  auto& cached = rows_[key];
  if (!cached) {
    cached = std::make_unique<Row>();
    auto& row = *cached;
    row.root = grid;
    row.container = container;
    container.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
    container.Padding(UniformThickness(0));
    const auto& t = kTokens.browser_tools;
    if (tasks_) {
      TaskColumns(grid);
      grid.MinHeight(t.row_height);
      const auto name = grid.Children().GetAt(0).as<Grid>();
      AddColumn(name, kTokens.app_menu.icon_size, xaml::GridUnitType::Pixel);
      AddColumn(name, 1, xaml::GridUnitType::Star);
      name.ColumnSpacing(t.gap);
      row.image = name.Children().GetAt(0).as<controls::Image>();
      row.image.Width(kTokens.app_menu.icon_size);
      row.image.Height(kTokens.app_menu.icon_size);
      row.icon = name.Children().GetAt(1).as<TextBlock>();
      row.icon.FontFamily(
          media::FontFamily(winrt::to_hstring(kTokens.type.icon_family)));
      row.icon.FontSize(kTokens.app_menu.icon_size);
      row.icon.VerticalAlignment(xaml::VerticalAlignment::Center);
      for (std::size_t i = 0; i < row.cells.size(); ++i) {
        row.cells[i] = i == 0 ? name.Children().GetAt(2).as<TextBlock>()
                              : grid.Children()
                                    .GetAt(static_cast<std::uint32_t>(i))
                                    .as<TextBlock>();
        row.cells[i].HorizontalAlignment(
            i ? xaml::HorizontalAlignment::Right
              : xaml::HorizontalAlignment::Stretch);
      }
    } else {
      grid.MinHeight(t.permission_row_height);
      AddColumn(grid, t.permission_icon_width, xaml::GridUnitType::Pixel);
      AddColumn(grid, 1, xaml::GridUnitType::Star);
      AddColumn(grid, t.permission_choice_width, xaml::GridUnitType::Pixel);
      grid.ColumnSpacing(t.gap);
      row.icon = grid.Children().GetAt(0).as<TextBlock>();
      row.icon.FontFamily(
          media::FontFamily(winrt::to_hstring(kTokens.type.icon_family)));
      row.icon.FontSize(kTokens.browser_tools.prompt_icon_size);
      row.icon.VerticalAlignment(xaml::VerticalAlignment::Center);
      const auto labels = grid.Children().GetAt(1).as<StackPanel>();
      labels.VerticalAlignment(xaml::VerticalAlignment::Center);
      for (std::size_t i = 0; i < 3; ++i)
        row.cells[i] = labels.Children()
                           .GetAt(static_cast<std::uint32_t>(i))
                           .as<TextBlock>();
      row.choices = grid.Children().GetAt(2).as<ComboBox>();
      row.choices.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
      row.choices.VerticalAlignment(xaml::VerticalAlignment::Center);
      auto* target = &row;
      row.changed = row.choices.SelectionChanged(
          winrt::auto_revoke, [this, target](auto&&, auto&&) {
            if (target->updating || binding_ || !target->id)
              return;
            const auto record = records_.find(target->id);
            const auto index = target->choices.SelectedIndex();
            if (record != records_.end() && index >= 0 &&
                static_cast<std::size_t>(index) < record->second.choice_count)
              context_.delegate.OnSitePermissionChanged(
                  PermissionId{target->id}, record->second.choices[index]);
          });
    }
    grid.Padding({kTokens.app_menu.row_padding_inline, 0,
                  kTokens.app_menu.row_padding_inline, 0});
    for (auto& cell : row.cells)
      if (cell) {
        cell.FontFamily(
            media::FontFamily(winrt::to_hstring(kTokens.type.family)));
        cell.FontSize(kTokens.type.size_small);
        cell.VerticalAlignment(xaml::VerticalAlignment::Center);
        cell.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
      }
    if (!tasks_)
      row.cells[0].FontSize(kTokens.type.size);
  }
  auto& row = *cached;
  if (const auto old = records_.find(row.id);
      old != records_.end() && old->second.row == &row)
    old->second.row = nullptr;
  row.id = winrt::unbox_value<std::uint64_t>(args.Item());
  if (const auto record = records_.find(row.id); record != records_.end()) {
    record->second.row = &row;
    UpdateRow(record->second);
  }
  args.Handled(true);
}
void BrowserToolWindow::UpdateRow(Record& record) {
  if (!record.row)
    return;
  auto& row = *record.row;
  row.updating = true;
  const auto name =
      tasks_ ? record.text[0] : record.text[0] + L", " + record.text[1];
  if (row.accessible_name != name) {
    row.accessible_name = name;
    Accessible(row.container, name);
  }
  if (tasks_) {
    record.image = context_.images.Prepare(
        context_.delegate.OnImageRequested(record.image_id));
    if (row.image.Source() != record.image)
      row.image.Source(record.image);
    row.image.Visibility(record.image ? xaml::Visibility::Visible
                                      : xaml::Visibility::Collapsed);
    row.icon.Visibility(record.image ? xaml::Visibility::Collapsed
                                     : xaml::Visibility::Visible);
    Text(row.icon, record.glyph);
    row.icon.Foreground(context_.brushes.icon);
    for (std::size_t i = 0; i < row.cells.size(); ++i) {
      Text(row.cells[i], record.text[i]);
      row.cells[i].Foreground(context_.brushes.text);
    }
    if (row.tooltip != record.text[5]) {
      row.tooltip = record.text[5];
      SetToolTip(row.container, row.tooltip);
    }
  } else {
    for (std::size_t i = 0; i < 3; ++i) {
      Text(row.cells[i], record.text[i]);
      row.cells[i].Foreground(i ? context_.brushes.muted
                                : context_.brushes.text);
    }
    Text(row.icon, record.glyph);
    row.icon.Foreground(context_.brushes.icon);
    const bool choices_changed =
        row.choice_count != record.choice_count ||
        !std::equal(record.choices.begin(),
                    record.choices.begin() +
                        static_cast<std::ptrdiff_t>(record.choice_count),
                    row.choice_values.begin());
    if (choices_changed) {
      row.choice_values = record.choices;
      row.choice_count = record.choice_count;
      row.choices.Items().Clear();
      for (std::size_t i = 0; i < record.choice_count; ++i)
        row.choices.Items().Append(winrt::box_value(
            setting_labels_[std::to_underlying(record.choices[i])]));
    }
    int selected = -1;
    for (std::size_t i = 0; i < record.choice_count; ++i)
      if (record.choices[i] == record.setting)
        selected = static_cast<int>(i);
    if (row.choices.SelectedIndex() != selected)
      row.choices.SelectedIndex(selected);
    row.choices.IsEnabled(record.editable);
    Accessible(row.choices, row.accessible_name);
  }
  row.updating = false;
}
bool BrowserToolWindow::ReconcileItems() {
  if (displayed_.empty() && !desired_.empty()) {
    items_.ReplaceAll(desired_);
    displayed_ = desired_;
    return true;
  }
  bool changed = false;
  for (std::size_t i = displayed_.size(); i > 0; --i) {
    const auto id = winrt::unbox_value<std::uint64_t>(displayed_[i - 1]);
    const auto found = records_.find(id);
    if (found == records_.end() || found->second.seen != generation_) {
      changed = true;
      items_.RemoveAt(static_cast<std::uint32_t>(i - 1));
      displayed_.erase(displayed_.begin() + static_cast<std::ptrdiff_t>(i - 1));
    }
  }
  for (std::size_t i = 0; i < desired_.size(); ++i) {
    if (i < displayed_.size() && displayed_[i] == desired_[i])
      continue;
    const auto start = displayed_.begin() + static_cast<std::ptrdiff_t>(
                                                std::min(i, displayed_.size()));
    if (const auto found = std::find(start, displayed_.end(), desired_[i]);
        found != displayed_.end()) {
      changed = true;
      items_.RemoveAt(static_cast<std::uint32_t>(found - displayed_.begin()));
      displayed_.erase(found);
    }
    changed = true;
    items_.InsertAt(static_cast<std::uint32_t>(i), desired_[i]);
    displayed_.insert(displayed_.begin() + static_cast<std::ptrdiff_t>(i),
                      desired_[i]);
  }
  std::erase_if(records_, [this](const auto& entry) {
    return entry.second.seen != generation_;
  });
  return changed;
}
void BrowserToolWindow::Reconcile(const BrowserToolsVm& vm) {
  ++generation_;
  desired_.clear();
  bool selection_changed = false;
  const auto append = [this](std::uint64_t id) -> Record& {
    auto& record = records_[id];
    if (!record.item)
      record.item = winrt::box_value(id);
    record.seen = generation_;
    desired_.push_back(record.item);
    return record;
  };
  if (tasks_) {
    for (const auto& task : vm.tasks) {
      auto& record = append(task.id.value);
      record.image_id = task.icon;

      if (record.category != task.category || record.glyph.empty()) {
        record.category = task.category;
        record.glyph = winrt::hstring(TaskIcon(task.category));
      }
      const std::array<std::string_view, 6> values{task.title,   task.memory,
                                                   task.cpu,     task.network,
                                                   task.process, task.profile};
      for (std::size_t i = 0; i < values.size(); ++i)
        if (record.utf8[i] != values[i]) {
          record.utf8[i] = values[i];
          record.text[i] = winrt::to_hstring(values[i]);
        }
      selection_changed |= record.selected != task.selected;
      record.selected = task.selected;
      UpdateRow(record);
    }
  } else {
    for (const auto& item : vm.permissions) {
      auto& record = append(item.id.value);
      if (record.kind != item.kind || record.glyph.empty()) {
        record.kind = item.kind;
        record.glyph = winrt::hstring(PermissionIcon(item.kind));
      }
      const std::array<std::string_view, 3> values{item.label, item.origin,
                                                   item.description};
      for (std::size_t i = 0; i < values.size(); ++i)
        if (record.utf8[i] != values[i]) {
          record.utf8[i] = values[i];
          record.text[i] = winrt::to_hstring(values[i]);
        }
      record.choices = item.choices;
      record.choice_count = item.choice_count;
      record.setting = item.setting;
      record.editable = item.editable;
      UpdateRow(record);
    }
  }
  const bool moved = ReconcileItems();
  if (tasks_ && (moved || selection_changed)) {
    auto selected = list_.SelectedItems();
    for (auto& [id, record] : records_)
      record.selection_present = false;
    for (std::uint32_t i = selected.Size(); i > 0; --i) {
      const auto id = winrt::unbox_value<std::uint64_t>(selected.GetAt(i - 1));
      const auto found = records_.find(id);
      if (found == records_.end() || !found->second.selected)
        selected.RemoveAt(i - 1);
      else
        found->second.selection_present = true;
    }
    for (const auto& item : desired_) {
      const auto& record = records_.at(winrt::unbox_value<std::uint64_t>(item));
      if (record.selected && !record.selection_present)
        selected.Append(item);
    }
  }
}
void BrowserToolWindow::Render(const ShellViewModel& vm) {
  const auto& tools = vm.browser_tools;
  const bool open = tasks_ ? tools.tasks_open : tools.permissions_open;
  if (!open) {
    if (visible_) {
      visible_ = false;
      const auto window = app_window_;
      hide_.Hide(input_target_.Window(), root_.DispatcherQueue(),
                 [window] { window.Hide(); });
      ReleaseImages();
    }
    return;
  }
  Build();
  hide_.BeforeShow();
  binding_ = true;
  struct Reset {
    bool& value;
    ~Reset() { value = false; }
  } reset{binding_};
  const auto theme =
      vm.theme.dark ? xaml::ElementTheme::Dark : xaml::ElementTheme::Light;
  if (root_.RequestedTheme() != theme)
    root_.RequestedTheme(theme);
  root_.FlowDirection(vm.text.right_to_left ? xaml::FlowDirection::RightToLeft
                                            : xaml::FlowDirection::LeftToRight);
  title_.Foreground(context_.brushes.text);
  subtitle_.Foreground(context_.brushes.muted);
  status_.Foreground(context_.brushes.muted);
  empty_.Foreground(context_.brushes.muted);
  const auto title_theme =
      context_.brushes.high_contrast()
          ? winrt::Microsoft::UI::Windowing::TitleBarTheme::UseDefaultAppMode
      : vm.theme.dark ? winrt::Microsoft::UI::Windowing::TitleBarTheme::Dark
                      : winrt::Microsoft::UI::Windowing::TitleBarTheme::Light;
  if (title_theme_ != title_theme) {
    title_theme_ = title_theme;
    app_window_.TitleBar().PreferredTheme(title_theme);
  }
  const auto revision =
      tasks_ ? tools.task_revision : tools.permission_revision;
  if (revision_ != revision || vm.changed.Has(Dirty::kImages)) {
    Reconcile(tools);
    revision_ = revision;
    const auto query = tasks_ ? tools.task_search : tools.permission_search;
    if (search_text_ != query) {
      search_text_ = query;
      const auto text = winrt::to_hstring(query);
      if (search_.Text() != text)
        search_.Text(text);
    }
    if (tasks_) {
      if (filter_.SelectedIndex() != static_cast<int>(tools.filter))
        filter_.SelectedIndex(static_cast<int>(tools.filter));
      const auto sort = std::pair{tools.sort, tools.ascending};
      if (sort_ != sort) {
        sort_ = sort;
        for (std::size_t i = 0; i < headers_.size(); ++i) {
          auto label = winrt::to_hstring(kTaskColumnLabels[i]);
          if (i == std::to_underlying(tools.sort))
            label = label + (tools.ascending ? L" \u2191" : L" \u2193");
          headers_[i]->control.Content(winrt::box_value(label));
        }
      }
    }
    const auto subtitle = tasks_ ? std::string_view{} : tools.origin;
    if (subtitle_text_ != subtitle) {
      subtitle_text_ = subtitle;
      Text(subtitle_, winrt::to_hstring(subtitle));
    }
    subtitle_.Visibility(subtitle.empty() ? xaml::Visibility::Collapsed
                                          : xaml::Visibility::Visible);
    const auto status = tasks_ ? tools.task_status : tools.permission_status;
    if (status_text_ != status) {
      status_text_ = status;
      Text(status_, winrt::to_hstring(status));
    }
    const bool empty = tasks_ ? tools.tasks.empty() : tools.permissions.empty();
    empty_.Visibility(empty ? xaml::Visibility::Visible
                            : xaml::Visibility::Collapsed);
    action_.IsEnabled(tasks_ ? tools.can_end : tools.can_reset);
    action_.Visibility(tasks_ || !tools.origin.empty()
                           ? xaml::Visibility::Visible
                           : xaml::Visibility::Collapsed);
    const auto error = tasks_ ? tools.task_error : tools.permission_error;
    if (error_text_ != error) {
      error_text_ = error;
      error_.Message(winrt::to_hstring(error));
      error_.IsOpen(!error.empty());
    }
  } else if (vm.changed.Has(Dirty::kTheme)) {
    for (auto& [id, record] : records_)
      UpdateRow(record);
  }
  const auto serial = tasks_ ? tools.task_serial : tools.permission_serial;
  if (!visible_ || serial_ != serial) {
    serial_ = serial;
    visible_ = true;
    window_.Activate();
  }
}
}  // namespace kr::win
