#include "settings_browser.hpp"

#include <winrt/Microsoft.UI.Xaml.Automation.h>

#include <algorithm>
#include <limits>

#include "krypton/ui/platform/platform.hpp"
#include "workspace_icon_picker.hpp"

namespace kr::win {
namespace {
controls::TextBlock Wrapped(double size) {
  auto label = Label(L"", size);
  label.FontFamily(
      media::FontFamily(winrt::to_hstring(kTokens.settings.font_family)));
  label.TextWrapping(xaml::TextWrapping::Wrap);
  return label;
}
}  // namespace

BrowserSettingsPanel::BrowserSettingsPanel(ViewContext& context,
                                           const SettingsStyle& style)
    : context_(context), style_(style) {
  root_.Spacing(kTokens.settings.section_gap);
  for (std::size_t index = 0; index < groups_.size(); ++index) {
    cards_[index] = style_.Card(groups_[index]);
    root_.Children().Append(cards_[index]);
  }
  workspaces_.Spacing(kTokens.settings.section_gap);
}
BrowserSettingsPanel::~BrowserSettingsPanel() {
  lifetime_.reset();
  DismissDialog();
}
void BrowserSettingsPanel::DismissDialog() {
  try {
    if (dialog_)
      dialog_.Hide();
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Settings dialog could not close");
  }
}
const winrt::hstring& BrowserSettingsPanel::Text(UiText key) const {
  return text_[static_cast<std::size_t>(key)];
}
BrowserSettingsPanel::Row& BrowserSettingsPanel::Build(
    const SettingsEntry& entry) {
  auto row = std::make_unique<Row>();
  auto& r = *row;
  r.entry = entry;
  r.grid.Padding({kTokens.settings.row_inset, kTokens.settings.row_padding,
                  kTokens.settings.row_inset, kTokens.settings.row_padding});
  r.grid.BorderBrush(style_.border);
  r.grid.MinHeight(kTokens.settings.row_min_height);
  r.grid.ColumnSpacing(kTokens.settings.control_gap);
  r.grid.RowSpacing(kTokens.settings.row_gap);
  AddColumn(r.grid, 1, xaml::GridUnitType::Star);
  AddColumn(r.grid, 0, xaml::GridUnitType::Auto);
  for (int i = 0; i < 3; ++i)
    AddRow(r.grid, 0, xaml::GridUnitType::Auto);
  r.labels.Spacing(kTokens.settings.label_gap);
  r.title = Wrapped(kTokens.settings.body_size);
  r.description = Wrapped(kTokens.settings.description_size);
  r.title.Foreground(style_.text);
  r.description.Foreground(style_.secondary);
  r.state = Wrapped(kTokens.settings.description_size);
  r.state.Foreground(style_.secondary);
  r.labels.Children().Append(r.title);
  r.labels.Children().Append(r.description);
  r.labels.Children().Append(r.state);
  r.grid.Children().Append(r.labels);
  switch (entry.kind) {
    case SettingsEntryKind::kStartup:
    case SettingsEntryKind::kSearch:
      r.choice = controls::ComboBox();
      r.control = r.choice;
      r.selected = r.choice.SelectionChanged(
          winrt::auto_revoke, [this, ptr = &r](auto&&, auto&&) {
            const auto index = ptr->choice.SelectedIndex();
            if (reconciling_ || index < 0)
              return;
            if (ptr->entry.kind == SettingsEntryKind::kStartup)
              context_.delegate.OnBrowserSettingEdited(
                  BrowserSetting::kStartup, static_cast<std::uint32_t>(index));
            else if (static_cast<std::size_t>(index) < ptr->engines.size())
              context_.delegate.OnSettingsSearchEngine(ptr->engines[index]);
          });
      break;
    case SettingsEntryKind::kAskDownload:
    case SettingsEntryKind::kWorkspaceSwipe:
      r.toggle = controls::ToggleSwitch();
      r.toggle.OnContent(winrt::box_value(L""));
      r.toggle.OffContent(winrt::box_value(L""));
      r.control = r.toggle;
      r.toggled = r.toggle.Toggled(
          winrt::auto_revoke, [this, ptr = &r](auto&&, auto&&) {
            if (reconciling_)
              return;
            if (ptr->entry.kind == SettingsEntryKind::kWorkspaceSwipe)
              context_.delegate.OnSettingsLayoutEdited(
                  {LayoutOption::kWorkspaceSwipe, ptr->toggle.IsOn()});
            else
              context_.delegate.OnBrowserSettingEdited(
                  BrowserSetting::kAskDownloadLocation,
                  ptr->toggle.IsOn() ? 1u : 0u);
          });
      break;
    case SettingsEntryKind::kProfile:
    case SettingsEntryKind::kVersion:
      r.value = Wrapped(kTokens.settings.body_size);
      r.value.IsTextSelectionEnabled(true);
      r.control = r.value;
      break;
    default:
      if (entry.kind == SettingsEntryKind::kAction) {
        r.button = controls::HyperlinkButton();
        r.value = Wrapped(kTokens.settings.body_size);
        r.button.Content(r.value);
        r.title.Visibility(xaml::Visibility::Collapsed);
        r.description.Visibility(xaml::Visibility::Collapsed);
        r.grid.MinHeight(kTokens.settings.row_height);
      } else
        r.button = controls::Button();
      r.control = r.button;
      r.clicked =
          r.button.Click(winrt::auto_revoke, [this, entry](auto&&, auto&&) {
            if (entry.kind == SettingsEntryKind::kWorkspaces)
              EditWorkspace({}, false);
            else if (entry.kind == SettingsEntryKind::kPermissions)
              context_.delegate.OnSitePermissionsRequested();
            else
              context_.delegate.OnSettingsAction(
                  entry.kind == SettingsEntryKind::kDefault
                      ? SettingsAction::kMakeDefault
                  : entry.kind == SettingsEntryKind::kLocation
                      ? SettingsAction::kChooseDownloadFolder
                      : entry.action);
          });
      break;
  }
  r.control.VerticalAlignment(xaml::VerticalAlignment::Center);
  if (const auto control = r.control.try_as<controls::Control>()) {
    control.FontSize(kTokens.settings.body_size);
    control.FontFamily(
        media::FontFamily(winrt::to_hstring(kTokens.settings.font_family)));
  }
  controls::Grid::SetColumn(r.control, 1);
  xaml::Automation::AutomationProperties::SetLabeledBy(r.control, r.title);
  r.grid.Children().Append(r.control);
  if (entry.kind == SettingsEntryKind::kWorkspaces) {
    controls::Grid::SetRow(workspaces_, 2);
    controls::Grid::SetColumnSpan(workspaces_, 2);
    r.grid.Children().Append(workspaces_);
  }
  r.sized = r.grid.SizeChanged(
      winrt::auto_revoke, [this, ptr = &r](auto&&, auto&&) { Reflow(*ptr); });
  const auto group = entry.kind == SettingsEntryKind::kAction ? 1u : 0u;
  groups_[group].Children().Append(r.grid);
  auto* result = row.get();
  rows_.emplace(entry.label, std::move(row));
  return *result;
}
void BrowserSettingsPanel::Reflow(Row& r) {
  const auto padding = r.grid.Padding();
  const auto available =
      static_cast<float>(r.grid.ActualWidth() - padding.Left - padding.Right);
  if (available <= 0)
    return;
  if (r.entry.kind == SettingsEntryKind::kAction) {
    controls::Grid::SetColumn(r.control, 0);
    controls::Grid::SetColumnSpan(r.control, 2);
    controls::Grid::SetRow(r.labels, 1);
    controls::Grid::SetColumnSpan(r.labels, 2);
    r.control.MaxWidth(available);
    r.control.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    return;
  }
  const auto infinity = std::numeric_limits<float>::infinity();
  r.labels.Measure(
      {std::min(available, kTokens.settings.label_max_width), infinity});
  r.control.MaxWidth(std::numeric_limits<double>::infinity());
  r.control.Measure({infinity, infinity});
  const bool fits = SettingsRowFits(available, r.labels.DesiredSize().Width,
                                    r.control.DesiredSize().Width,
                                    static_cast<float>(r.grid.ColumnSpacing()));
  r.control.MaxWidth(available);
  controls::Grid::SetRow(r.control, fits ? 0 : 1);
  controls::Grid::SetColumn(r.control, fits ? 1 : 0);
  controls::Grid::SetColumnSpan(r.labels, fits ? 1 : 2);
  controls::Grid::SetColumnSpan(r.control, fits ? 1 : 2);
}
void BrowserSettingsPanel::Render(const ShellViewModel::SettingsVm& vm,
                                  UiTextView text,
                                  bool refresh_text,
                                  bool refresh_workspaces) {
  if (text_ready_ && !refresh_text && !refresh_workspaces &&
      section_ == vm.state.section && layout_ == vm.layout &&
      browser_revision_ == vm.browser->revision &&
      service_revision_ == vm.services->revision)
    return;
  section_ = vm.state.section;
  layout_ = vm.layout;
  browser_revision_ = vm.browser->revision;
  service_revision_ = vm.services->revision;
  reconciling_ = true;
  struct Reset {
    bool& value;
    ~Reset() { value = false; }
  } reset{reconciling_};
  if (!text_ready_ || refresh_text) {
    for (std::size_t i = 0; i < text_.size(); ++i)
      text_[i] = NativeText(static_cast<UiText>(i), text);
    text_ready_ = true;
  }
  for (auto& [key, row] : rows_) {
    (void)key;
    row->grid.Visibility(xaml::Visibility::Collapsed);
  }
  const auto& state = *vm.browser;
  const auto& value = state.value;
  for (const auto& entry : vm.entries) {
    if (entry.section != vm.state.section ||
        entry.kind == SettingsEntryKind::kAppearance ||
        entry.kind == SettingsEntryKind::kLayout)
      continue;
    auto found = rows_.find(entry.label);
    auto& r = found == rows_.end() ? Build(entry) : *found->second;
    r.grid.Visibility(xaml::Visibility::Visible);
    r.title.Text(Text(entry.label));
    r.description.Text(Text(entry.description));
    r.state.Text(L"");
    if (const auto control = r.control.try_as<controls::Control>())
      control.IsEnabled(true);
    if (r.button) {
      if (entry.kind == SettingsEntryKind::kAction)
        r.value.Text(Text(entry.label));
      else
        r.button.Content(winrt::box_value(Text(entry.label)));
    }
    std::optional<SettingsControl> control;
    switch (entry.kind) {
      case SettingsEntryKind::kDefault: {
        control = SettingsControl::kDefaultBrowser;
        const bool is_default =
            vm.services->default_browser.Has(DefaultBrowserFlag::kIsDefault);
        r.description.Text(Text(is_default ? UiText::kSettingsIsDefault
                                           : UiText::kSettingsNotDefault));
        r.button.Content(winrt::box_value(Text(UiText::kSettingsMakeDefault)));
        r.button.Visibility(!is_default &&
                                    state.Supports(SettingsAction::kMakeDefault)
                                ? xaml::Visibility::Visible
                                : xaml::Visibility::Collapsed);
        break;
      }
      case SettingsEntryKind::kStartup:
        control = SettingsControl::kStartup;
        if (refresh_text || !r.choice.Items().Size()) {
          r.choice.Items().Clear();
          for (const auto key :
               {UiText::kSettingsStartupNew, UiText::kSettingsStartupContinue,
                UiText::kSettingsStartupPages})
            r.choice.Items().Append(winrt::box_value(Text(key)));
        }
        r.choice.SelectedIndex(static_cast<int>(value.startup));
        r.control.as<controls::Control>().IsEnabled(
            state.editable && !state.Pending(BrowserSetting::kStartup));
        if (state.Pending(BrowserSetting::kStartup))
          r.state.Text(Text(UiText::kSettingsPending));
        break;
      case SettingsEntryKind::kAskDownload:
        control = SettingsControl::kAskDownloadLocation;
        r.toggle.IsOn(value.ask_download_location);
        r.control.as<controls::Control>().IsEnabled(
            state.editable &&
            !state.Pending(BrowserSetting::kAskDownloadLocation));
        if (state.Pending(BrowserSetting::kAskDownloadLocation))
          r.state.Text(Text(UiText::kSettingsPending));
        break;
      case SettingsEntryKind::kWorkspaceSwipe:
        r.toggle.IsOn(vm.layout.flags.Has(LayoutFlag::kWorkspaceSwipe));
        r.control.as<controls::Control>().IsEnabled(vm.state.layout_editable);
        break;
      case SettingsEntryKind::kLocation:
        control = SettingsControl::kDownloadLocation;
        r.description.Text(winrt::to_hstring(value.download_location));
        r.button.Content(winrt::box_value(Text(UiText::kSettingsChangeFolder)));
        r.button.Visibility(
            state.Supports(SettingsAction::kChooseDownloadFolder)
                ? xaml::Visibility::Visible
                : xaml::Visibility::Collapsed);
        break;
      case SettingsEntryKind::kProfile:
        control = SettingsControl::kProfile;
        r.value.Text(winrt::to_hstring(value.profile_name));
        break;
      case SettingsEntryKind::kVersion:
        r.value.Text(winrt::to_hstring(value.version));
        r.title.Text(winrt::to_hstring(value.product));
        r.description.Text(Text(UiText::kSettingsBuild) + L": " +
                           winrt::to_hstring(value.build));
        break;
      case SettingsEntryKind::kSearch: {
        control = SettingsControl::kSearch;
        bool changed =
            refresh_text || r.engines.size() != vm.services->engines.size();
        for (std::size_t i = 0; !changed && i < r.engines.size(); ++i)
          changed = r.engines[i] != vm.services->engines[i].id ||
                    winrt::unbox_value<winrt::hstring>(r.choice.Items().GetAt(
                        static_cast<std::uint32_t>(i))) !=
                        winrt::to_hstring(vm.services->engines[i].name);
        if (changed) {
          r.engines.clear();
          r.choice.Items().Clear();
          for (const auto& engine : vm.services->engines) {
            r.engines.push_back(engine.id);
            r.choice.Items().Append(
                winrt::box_value(winrt::to_hstring(engine.name)));
          }
        }
        const auto current =
            std::ranges::find(r.engines, vm.services->DefaultEngine());
        r.choice.SelectedIndex(
            current == r.engines.end()
                ? -1
                : static_cast<int>(current - r.engines.begin()));
        r.control.as<controls::Control>().IsEnabled(
            vm.can_choose_search && !vm.services->SearchManaged() &&
            !state.pending_engine);
        if (state.pending_engine)
          r.state.Text(Text(UiText::kSettingsPending));
        if (vm.services->SearchManaged())
          r.state.Text(Text(UiText::kSettingsManaged));
        break;
      }
      case SettingsEntryKind::kWorkspaces:
        r.button.Content(
            winrt::box_value(Text(UiText::kSettingsCreateWorkspace)));
        r.button.Visibility(vm.can_edit_workspaces
                                ? xaml::Visibility::Visible
                                : xaml::Visibility::Collapsed);
        Workspaces(vm);
        break;
      default:
        break;
    }
    if (control && state.Managed(*control)) {
      if (const auto native = r.control.try_as<controls::Control>())
        native.IsEnabled(false);
      r.state.Text(Text(UiText::kSettingsManaged));
    }
    if (r.button && entry.kind != SettingsEntryKind::kWorkspaces &&
        entry.kind != SettingsEntryKind::kPermissions && state.action.request) {
      r.button.IsEnabled(false);
      if (state.action.action == entry.action ||
          (entry.kind == SettingsEntryKind::kDefault &&
           state.action.action == SettingsAction::kMakeDefault) ||
          (entry.kind == SettingsEntryKind::kLocation &&
           state.action.action == SettingsAction::kChooseDownloadFolder))
        r.state.Text(Text(UiText::kSettingsPending));
    }
    xaml::Automation::AutomationProperties::SetHelpText(
        r.control, r.description.Text() + L" " + r.state.Text());
    r.state.Visibility(r.state.Text().empty() ? xaml::Visibility::Collapsed
                                              : xaml::Visibility::Visible);
    Reflow(r);
  }
  bool has_content = false;
  for (std::size_t index = 0; index < groups_.size(); ++index) {
    bool first = true;
    for (const auto& child : groups_[index].Children()) {
      const auto row = child.as<controls::Grid>();
      if (row.Visibility() != xaml::Visibility::Visible)
        continue;
      const xaml::Thickness divider{
          0, first ? 0 : kTokens.settings.border_width, 0, 0};
      if (row.BorderThickness() != divider)
        row.BorderThickness(divider);
      first = false;
    }
    cards_[index].Visibility(first ? xaml::Visibility::Collapsed
                                   : xaml::Visibility::Visible);
    has_content |= !first;
  }
  root_.Visibility(has_content ? xaml::Visibility::Visible
                               : xaml::Visibility::Collapsed);
}
void BrowserSettingsPanel::Workspaces(const ShellViewModel::SettingsVm& vm) {
  for (auto it = spaces_.begin(); it != spaces_.end();) {
    if (std::ranges::find(vm.workspaces, it->first, &Space::id) ==
        vm.workspaces.end()) {
      std::uint32_t index = 0;
      if (workspaces_.Children().IndexOf(it->second->root, index))
        workspaces_.Children().RemoveAt(index);
      it = spaces_.erase(it);
    } else
      ++it;
  }
  std::uint32_t position = 0;
  for (const auto& space : vm.workspaces) {
    auto& item = spaces_[space.id];
    if (!item) {
      item = std::make_unique<WorkspaceRow>();
      auto& row = *item;
      row.name = Wrapped(kTokens.settings.body_size);
      row.name.FontWeight(kSemiBold);
      row.state = Wrapped(kTokens.settings.description_size);
      row.state.Foreground(context_.brushes.muted);
      row.root.Spacing(kTokens.settings.label_gap);
      controls::StackPanel heading;
      heading.Orientation(controls::Orientation::Horizontal);
      heading.Spacing(kTokens.settings.label_gap);
      row.icon = Glyph(L"", kTokens.spacing.icon_size);
      heading.Children().Append(row.icon);
      heading.Children().Append(row.name);
      row.root.Children().Append(heading);
      row.root.Children().Append(row.state);
      row.commands.DefaultLabelPosition(
          controls::CommandBarDefaultLabelPosition::Right);
      row.commands.HorizontalAlignment(xaml::HorizontalAlignment::Left);
      row.commands.IsOpen(false);
      row.activate.Icon(controls::SymbolIcon(controls::Symbol::Forward));
      row.edit.Icon(controls::SymbolIcon(controls::Symbol::Edit));
      row.remove.Icon(controls::SymbolIcon(controls::Symbol::Delete));
      row.commands.PrimaryCommands().Append(row.activate);
      row.commands.PrimaryCommands().Append(row.edit);
      row.commands.SecondaryCommands().Append(row.remove);
      row.root.Children().Append(row.commands);
      row.activated = row.activate.Click(
          winrt::auto_revoke, [this, id = space.id](auto&&, auto&&) {
            context_.delegate.OnSpaceSelected(id);
          });
      row.edited = row.edit.Click(winrt::auto_revoke,
                                  [this, ptr = &row](auto&&, auto&&) {
                                    EditWorkspace(ptr->space, false);
                                  });
      row.removed = row.remove.Click(winrt::auto_revoke,
                                     [this, ptr = &row](auto&&, auto&&) {
                                       EditWorkspace(ptr->space, true);
                                     });
      workspaces_.Children().Append(row.root);
    }
    auto& row = *item;
    row.space = space;
    row.name.Text(winrt::to_hstring(space.name));
    const auto symbol = winrt::to_hstring(space.glyph);
    row.icon.Text(symbol);
    row.icon.Foreground(context_.brushes.icon);
    const bool native_icon =
        symbol.size() == 1 && symbol[0] >= 0xE000 && symbol[0] <= 0xF8FF;
    row.icon.FontFamily(media::FontFamily(winrt::to_hstring(
        native_icon ? kTokens.type.icon_family
                    : kTokens.workspace_picker.emoji_family)));
    const bool active = space.id == vm.active_workspace;
    row.state.Text(active ? Text(UiText::kSettingsActiveWorkspace)
                          : winrt::hstring{});
    row.activate.Label(Text(UiText::kSettingsSwitchWorkspace));
    row.activate.IsEnabled(!active);
    row.activate.Visibility(vm.state.workspace_switchable
                                ? xaml::Visibility::Visible
                                : xaml::Visibility::Collapsed);
    row.edit.Label(Text(UiText::kSettingsEditWorkspace));
    row.remove.Label(Text(UiText::kSettingsDeleteWorkspace));
    row.edit.Visibility(vm.can_edit_workspaces ? xaml::Visibility::Visible
                                               : xaml::Visibility::Collapsed);
    row.remove.Visibility(vm.can_edit_workspaces &&
                                  space.id != vm.workspaces.front().id
                              ? xaml::Visibility::Visible
                              : xaml::Visibility::Collapsed);
    std::uint32_t old = 0;
    if (workspaces_.Children().IndexOf(row.root, old) && old != position) {
      workspaces_.Children().RemoveAt(old);
      workspaces_.Children().InsertAt(position, row.root);
    }
    ++position;
  }
}
void BrowserSettingsPanel::EditWorkspace(Space value, bool remove) {
  if (dialog_)
    dialog_.Hide();
  controls::ContentDialog dialog;
  dialog.XamlRoot(root_.XamlRoot());
  dialog.RequestedTheme(root_.ActualTheme());
  dialog.FlowDirection(root_.FlowDirection());
  dialog.Title(
      winrt::box_value(Text(remove     ? UiText::kSettingsDeleteWorkspace
                            : value.id ? UiText::kSettingsEditWorkspace
                                       : UiText::kSettingsCreateWorkspace)));
  dialog.PrimaryButtonText(
      Text(remove ? UiText::kSettingsDeleteWorkspace : UiText::kWorkspaceSave));
  dialog.CloseButtonText(Text(UiText::kWorkspaceCancel));
  dialog.DefaultButton(controls::ContentDialogButton::Primary);
  controls::StackPanel content;
  content.Spacing(kTokens.settings.row_gap);
  controls::TextBox name;
  const auto glyph = std::make_shared<WorkspaceIconPicker>(context_);
  workspace_picker_ = glyph;
  name.Header(winrt::box_value(Text(UiText::kSettingsWorkspaceName)));
  name.Text(winrt::to_hstring(value.name));
  glyph->SetLabels(
      {Text(UiText::kSettingsWorkspaceIcon), Text(UiText::kWorkspaceChooseIcon),
       Text(UiText::kWorkspaceSearchIcons), Text(UiText::kWorkspaceIcons),
       Text(UiText::kWorkspaceEmoji), Text(UiText::kWorkspaceCustomEmoji)});
  glyph->SetValue(value.glyph);
  auto error = Wrapped(kTokens.settings.description_size);
  if (remove) {
    auto description = Wrapped(kTokens.settings.body_size);
    description.Text(Text(UiText::kSettingsDeleteWorkspaceHelp));
    content.Children().Append(description);
  } else {
    content.Children().Append(name);
    content.Children().Append(glyph->Root());
  }
  content.Children().Append(error);
  dialog.Content(content);
  dialog_ = dialog;
  // The coroutine owns both dialog and token; no callback outlives its view.
  const auto weak = std::weak_ptr<int>(lifetime_);
  const auto required = Text(UiText::kWorkspaceNameRequired);
  auto* delegate = &context_.delegate;
  const std::weak_ptr<WorkspaceIconPicker> weak_glyph = glyph;
  auto clicked = dialog.PrimaryButtonClick(
      winrt::auto_revoke,
      [weak, delegate, value, remove, name, weak_glyph, error, required](
          auto&&, const controls::ContentDialogButtonClickEventArgs& args) {
        if (weak.expired()) {
          args.Cancel(true);
          return;
        }
        const auto glyph = weak_glyph.lock();
        if (!glyph) {
          args.Cancel(true);
          return;
        }
        const auto result = delegate->OnSettingsWorkspaceSubmitted(
            remove ? WorkspaceEditKind::kRemove : WorkspaceEditKind::kUpsert,
            value.id, winrt::to_string(name.Text()), glyph->Value());
        if (!result) {
          args.Cancel(true);
          error.Text(required);
        }
      });
  const auto run = [](controls::ContentDialog retained,
                      controls::ContentDialog::PrimaryButtonClick_revoker token)
      -> winrt::fire_and_forget {
    (void)token;
    try {
      co_await retained.ShowAsync();
    } catch (const winrt::hresult_error&) {
      platform::WriteDefaultLog(LogLevel::kWarning,
                                "Workspace settings dialog could not open");
    }
  };
  run(dialog, std::move(clicked));
}
bool BrowserSettingsPanel::Focus(UiText label) {
  const auto found = rows_.find(label);
  if (found == rows_.end() ||
      found->second->grid.Visibility() != xaml::Visibility::Visible)
    return false;
  found->second->grid.StartBringIntoView();
  return found->second->control.Focus(xaml::FocusState::Programmatic);
}
}  // namespace kr::win
