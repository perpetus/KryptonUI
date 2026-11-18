#include "settings_browser.hpp"

#include <winrt/Microsoft.UI.Xaml.Automation.h>

#include <algorithm>
#include <format>
#include <limits>
#include <utility>

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

LocalModelsPanel::LocalModelsPanel(ViewContext& context,
                                   const SettingsStyle& style)
    : context_(context) {
  root_.Spacing(kTokens.settings.section_gap);
  resources_ = Wrapped(kTokens.settings.body_size);
  consent_ = Wrapped(kTokens.settings.description_size);
  resources_.Foreground(style.text);
  consent_.Foreground(style.secondary);
  root_.Children().Append(resources_);
  root_.Children().Append(consent_);
  for (auto& group : groups_) {
    group.content.Spacing(kTokens.settings.row_gap);
    group.content.Padding(UniformThickness(kTokens.settings.row_inset));
    group.picker.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    group.picker.MaxDropDownHeight(kTokens.settings.dropdown_max_height);
    group.status = Wrapped(kTokens.settings.description_size);
    group.status.Foreground(style.secondary);
    group.active = Wrapped(kTokens.settings.description_size);
    group.active.Foreground(style.text);
    group.content.Children().Append(group.picker);
    if (&group == &groups_[1]) {
      voice_heading_ = Wrapped(kTokens.settings.body_size);
      voice_heading_.FontWeight(kSemiBold);
      voice_heading_.Foreground(style.text);
      group.content.Children().Append(voice_heading_);
      for (std::size_t axis = 0; axis < voice_pickers_.size(); ++axis) {
        const auto& picker = voice_pickers_[axis];
        picker.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
        picker.MaxDropDownHeight(kTokens.settings.dropdown_max_height);
        group.content.Children().Append(picker);
        voice_changed_[axis] =
            picker.SelectionChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
              if (!reconciling_ && structured_voice_) {
                ReconcileVoicePickers();
                UpdateSelected(groups_[1]);
              }
            });
      }
    }
    group.content.Children().Append(group.active);
    group.content.Children().Append(group.status);
    group.content.Children().Append(group.transfer);
    group.content.Children().Append(group.select);
    group.downloaded = Wrapped(kTokens.settings.body_size);
    group.downloaded.FontWeight(kSemiBold);
    group.downloaded.Foreground(style.text);
    group.empty = Wrapped(kTokens.settings.description_size);
    group.empty.Foreground(style.secondary);
    group.installed.Spacing(kTokens.settings.row_gap);
    group.content.Children().Append(group.downloaded);
    group.content.Children().Append(group.empty);
    group.content.Children().Append(group.installed);
    group.changed = group.picker.SelectionChanged(
        winrt::auto_revoke, [this, row = &group](auto&&, auto&&) {
          if (!reconciling_)
            UpdateSelected(*row);
        });
    group.transferred = group.transfer.Click(
        winrt::auto_revoke, [this, row = &group](auto&&, auto&&) {
          const auto* model = Selected(*row);
          if (!model)
            return;
          Invoke(*row, model->flags.Has(LocalModelFlag::kInstalled)
                           ? LocalModelCommand::kRemove
                           : LocalModelCommand::kDownload);
        });
    group.selected = group.select.Click(
        winrt::auto_revoke, [this, row = &group](auto&&, auto&&) {
          Invoke(*row, LocalModelCommand::kSelect);
        });
    group.card = style.Card(group.content);
    root_.Children().Append(group.card);
  }
}

LocalModelsPanel::~LocalModelsPanel() {
  lifetime_.reset();
  DismissDialog();
}

void LocalModelsPanel::DismissDialog() {
  if (auto dialog = std::exchange(dialog_, controls::ContentDialog{nullptr})) {
    try {
      dialog.Hide();
    } catch (...) {
      platform::WriteDefaultLog(LogLevel::kWarning,
                                "Model settings dialog could not close");
    }
  }
}

void LocalModelsPanel::Render(const LocalModelsState& state,
                              UiTextView text,
                              bool refresh_text) {
  if (state_ == state && !refresh_text)
    return;
  state_ = state;
  if (refresh_text || download_.empty()) {
    resources_.Text(NativeText(UiText::kSettingsLocalModelResources, text));
    consent_.Text(NativeText(UiText::kSettingsLocalModelConsent, text));
    checking_ = NativeText(UiText::kSettingsModelChecking, text);
    installed_ = NativeText(UiText::kSettingsModelInstalled, text);
    missing_ = NativeText(UiText::kSettingsModelNotInstalled, text);
    download_ = NativeText(UiText::kSettingsModelDownload, text);
    downloading_ = NativeText(UiText::kSettingsModelDownloading, text);
    remove_ = NativeText(UiText::kSettingsModelRemove, text);
    downloaded_ = NativeText(UiText::kSettingsDownloadedModels, text);
    no_downloads_ = NativeText(UiText::kSettingsNoDownloadedModels, text);
    active_voice_ = NativeText(UiText::kSettingsModelActiveVoice, text);
    first_use_titles_ = {
        NativeText(UiText::kSettingsModelTranslationFirstUse, text),
        NativeText(UiText::kSettingsModelVoiceFirstUse, text)};
    first_use_text_ = {
        NativeText(UiText::kSettingsModelTranslationPrivate, text),
        NativeText(UiText::kSettingsModelVoicePrivate, text)};
    continue_ = NativeText(UiText::kSettingsModelContinue, text);
    cancel_ = NativeText(UiText::kPrivacyCancel, text);
    groups_[0].picker.Header(
        winrt::box_value(NativeText(UiText::kSettingsTranslationModels, text)));
    groups_[1].picker.Header(
        winrt::box_value(NativeText(UiText::kSettingsVoiceModels, text)));
    voice_heading_.Text(NativeText(UiText::kSettingsVoiceModels, text));
    constexpr std::array headings{UiText::kSettingsVoiceLanguage,
                                  UiText::kSettingsVoiceModel,
                                  UiText::kSettingsVoiceSize};
    for (std::size_t axis = 0; axis < headings.size(); ++axis) {
      voice_pickers_[axis].Header(
          winrt::box_value(NativeText(headings[axis], text)));
    }
    voice_sizes_ = {NativeText(UiText::kSettingsVoiceTiny, text),
                    NativeText(UiText::kSettingsVoiceSmall, text),
                    NativeText(UiText::kSettingsVoiceMedium, text)};
    groups_[1].select.Content(
        winrt::box_value(NativeText(UiText::kSettingsModelUseVoice, text)));
  }
  reconciling_ = true;
  struct Reset {
    bool& value;
    ~Reset() { value = false; }
  } reset{reconciling_};
  for (std::size_t index = 0; index < groups_.size(); ++index) {
    auto& group = groups_[index];
    std::vector<LocalModelRow> rows;
    for (const auto& row : state.rows) {
      if (std::to_underlying(row.kind) == index)
        rows.push_back(row);
    }
    const auto current = group.picker.SelectedIndex();
    const bool same_catalog = std::ranges::equal(
        group.rows, rows, [](const auto& old_row, const auto& new_row) {
          return old_row.id == new_row.id && old_row.name == new_row.name &&
                 old_row.voice == new_row.voice;
        });
    const auto active = std::ranges::find_if(rows, [](const auto& row) {
      return row.flags.Has(LocalModelFlag::kActive);
    });
    if (active != rows.end()) {
      const auto label = winrt::hstring(
          std::wstring(active_voice_.c_str(), active_voice_.size()) + L" — " +
          winrt::to_hstring(active->name).c_str());
      if (group.active.Text() != label)
        group.active.Text(label);
    }
    group.active.Visibility(active == rows.end() ? xaml::Visibility::Collapsed
                                                 : xaml::Visibility::Visible);
    group.select.Visibility(index == 1 ? xaml::Visibility::Visible
                                       : xaml::Visibility::Collapsed);
    group.card.Visibility(rows.empty() ? xaml::Visibility::Collapsed
                                       : xaml::Visibility::Visible);
    if (index == 1) {
      structured_voice_ =
          !rows.empty() && std::ranges::all_of(rows, [](const auto& row) {
            return !!row.voice;
          });
      const auto visibility = structured_voice_ ? xaml::Visibility::Visible
                                                : xaml::Visibility::Collapsed;
      group.picker.Visibility(structured_voice_ ? xaml::Visibility::Collapsed
                                                : xaml::Visibility::Visible);
      voice_heading_.Visibility(visibility);
      for (const auto& picker : voice_pickers_)
        picker.Visibility(visibility);
    }
    if (!same_catalog && !(index == 1 && structured_voice_)) {
      const auto selected =
          current >= 0 && static_cast<size_t>(current) < group.rows.size()
              ? std::optional(group.rows[current].id)
              : std::nullopt;
      group.picker.Items().Clear();
      // Keep the user's choice; otherwise the active model, then the host's
      // detected default for this kind.
      const auto rank = [&](const LocalModelRow& row) {
        if (selected)
          return selected == row.id ? 3 : 0;
        return row.flags.Has(LocalModelFlag::kActive)      ? 2
               : row.flags.Has(LocalModelFlag::kSuggested) ? 1
                                                           : 0;
      };
      int32_t next = -1;
      int best = 0;
      for (size_t item = 0; item < rows.size(); ++item) {
        const auto& row = rows[item];
        group.picker.Items().Append(
            winrt::box_value(winrt::to_hstring(row.name)));
        if (const int score = rank(row); score > best) {
          best = score;
          next = static_cast<int32_t>(item);
        }
      }
      if (!rows.empty())
        group.picker.SelectedIndex(std::max(0, next));
    }
    group.rows = std::move(rows);
    ReconcileInstalled(group);
    if (index == 1 && structured_voice_ &&
        (!same_catalog || refresh_text || !voice_pack_))
      ReconcileVoicePickers();
    UpdateSelected(group);
  }
}

void LocalModelsPanel::ReconcileInstalled(Group& group) {
  group.downloaded.Text(downloaded_);
  group.empty.Text(no_downloads_);
  std::erase_if(group.installed_rows, [&](const auto& entry) {
    const auto found =
        std::ranges::find(group.rows, entry.first, &LocalModelRow::id);
    if (found != group.rows.end() &&
        found->flags.Has(LocalModelFlag::kInstalled))
      return false;
    std::uint32_t index = 0;
    if (group.installed.Children().IndexOf(entry.second->root, index))
      group.installed.Children().RemoveAt(index);
    return true;
  });
  for (const auto& model : group.rows) {
    if (!model.flags.Has(LocalModelFlag::kInstalled))
      continue;
    auto& retained = group.installed_rows[model.id];
    if (!retained) {
      retained = std::make_unique<InstalledRow>();
      auto& row = *retained;
      row.root.ColumnSpacing(kTokens.settings.row_gap);
      AddColumn(row.root, 1, xaml::GridUnitType::Star);
      AddColumn(row.root, 0, xaml::GridUnitType::Auto);
      row.name = Wrapped(kTokens.settings.body_size);
      row.name.Foreground(resources_.Foreground());
      row.name.VerticalAlignment(xaml::VerticalAlignment::Center);
      controls::Grid::SetColumn(row.remove, 1);
      row.root.Children().Append(row.name);
      row.root.Children().Append(row.remove);
      row.clicked = row.remove.Click(
          winrt::auto_revoke,
          [this, kind = model.kind, id = model.id](auto&&, auto&&) {
            context_.delegate.OnLocalModelCommand(kind, id,
                                                  LocalModelCommand::kRemove);
          });
      group.installed.Children().Append(row.root);
    }
    const auto label =
        winrt::to_hstring(model.name) +
        (model.flags.Has(LocalModelFlag::kActive) ? L" · " + active_voice_
                                                  : winrt::hstring{});
    if (retained->name.Text() != label)
      retained->name.Text(label);
    retained->remove.Content(winrt::box_value(remove_));
    Accessible(retained->remove,
               remove_ + L" " + winrt::to_hstring(model.name));
    retained->remove.IsEnabled(!state_.checking &&
                               model.flags.Has(LocalModelFlag::kCanRemove));
  }
  group.empty.Visibility(group.installed_rows.empty()
                             ? xaml::Visibility::Visible
                             : xaml::Visibility::Collapsed);
}

void LocalModelsPanel::ReconcileVoicePickers() {
  const bool was_reconciling = std::exchange(reconciling_, true);
  struct Restore {
    bool& value;
    bool previous;
    ~Restore() { value = previous; }
  } restore{reconciling_, was_reconciling};

  const auto& rows = groups_[1].rows;
  std::vector<const LocalModelRow*> candidates;
  candidates.reserve(rows.size());
  const VoiceModelVariant* active = nullptr;
  const VoiceModelVariant* suggested = nullptr;
  for (const auto& row : rows) {
    candidates.push_back(&row);
    if (row.flags.Has(LocalModelFlag::kActive))
      active = &*row.voice;
    if (!suggested && row.flags.Has(LocalModelFlag::kSuggested))
      suggested = &*row.voice;
  }
  // Without a choice, start from the active pack, then the detected default.
  const VoiceModelVariant* preferred = active ? active : suggested;

  const auto choice = [this](const VoiceModelVariant& voice,
                             std::size_t axis) -> VoiceChoice {
    if (axis == 0)
      return {voice.language_tag, winrt::to_hstring(voice.language_name)};
    if (axis == 1)
      return {voice.family, winrt::to_hstring(voice.family)};
    if (voice.size == VoiceModelSize::kTiny)
      return {"tiny", voice_sizes_[0]};
    if (voice.size == VoiceModelSize::kMedium)
      return {"medium", voice_sizes_[2]};
    return {"small", voice_sizes_[1]};
  };

  for (std::size_t axis = 0; axis < voice_pickers_.size(); ++axis) {
    const auto& picker = voice_pickers_[axis];
    auto& previous = voice_choices_[axis];
    const auto current = picker.SelectedIndex();
    const auto desired =
        current >= 0 && static_cast<std::size_t>(current) < previous.size()
            ? previous[current].key
        : preferred ? choice(*preferred, axis).key
                    : std::string{};
    std::vector<VoiceChoice> options;
    for (const auto* row : candidates) {
      auto option = choice(*row->voice, axis);
      if (std::ranges::find(options, option.key, &VoiceChoice::key) ==
          options.end())
        options.push_back(std::move(option));
    }
    if (axis == 0) {
      std::ranges::sort(options, [](const auto& a, const auto& b) {
        return std::wstring_view(a.label) < std::wstring_view(b.label);
      });
    }
    auto found = std::ranges::find(options, desired, &VoiceChoice::key);
    if (found == options.end())
      found = options.begin();
    const auto selection = static_cast<std::int32_t>(found - options.begin());
    const auto selected_key = found->key;
    if (previous != options) {
      picker.Items().Clear();
      for (const auto& option : options)
        picker.Items().Append(winrt::box_value(option.label));
      previous = std::move(options);
    }
    if (picker.SelectedIndex() != selection)
      picker.SelectedIndex(selection);
    std::erase_if(candidates, [&](const auto* row) {
      return choice(*row->voice, axis).key != selected_key;
    });
  }
  voice_pack_ = candidates.front()->id;
}

const LocalModelRow* LocalModelsPanel::Selected(const Group& group) const {
  if (&group == &groups_[1] && structured_voice_) {
    if (!voice_pack_)
      return nullptr;
    const auto found =
        std::ranges::find(group.rows, *voice_pack_, &LocalModelRow::id);
    return found == group.rows.end() ? nullptr : &*found;
  }
  const auto index = group.picker.SelectedIndex();
  return index >= 0 && static_cast<std::size_t>(index) < group.rows.size()
             ? &group.rows[index]
             : nullptr;
}

void LocalModelsPanel::UpdateSelected(Group& group) {
  const auto* selected = Selected(group);
  if (!selected) {
    group.transfer.IsEnabled(false);
    group.select.IsEnabled(false);
    return;
  }
  const auto& row = *selected;
  const bool installed = row.flags.Has(LocalModelFlag::kInstalled);
  group.transfer.Visibility(installed ? xaml::Visibility::Collapsed
                                      : xaml::Visibility::Visible);
  const auto distributor =
      row.kind == LocalModelKind::kTranslation ? L"Mozilla" : L"Moonshine AI";
  const auto status =
      state_.checking ? checking_
      : row.progress >= 0
          ? winrt::hstring(std::format(
                L"{} {}%", std::wstring_view(downloading_), row.progress))
      : !row.error.empty() ? winrt::to_hstring(row.error)
      : installed          ? installed_
                           : missing_;
  if (group.status.Text() != status)
    group.status.Text(status);
  const auto action =
      installed ? remove_
                : winrt::hstring(std::format(
                      L"{} · {} MB · {}", std::wstring_view(download_),
                      row.download_bytes / 1'000'000 +
                          (row.download_bytes % 1'000'000 != 0),
                      distributor));
  if (winrt::unbox_value_or(group.transfer.Content(), winrt::hstring{}) !=
      action)
    group.transfer.Content(winrt::box_value(action));
  group.transfer.IsEnabled(!state_.checking &&
                           row.flags.Has(installed
                                             ? LocalModelFlag::kCanRemove
                                             : LocalModelFlag::kCanDownload));
  group.select.IsEnabled(!state_.checking &&
                         row.flags.Has(LocalModelFlag::kCanSelect));
}

void LocalModelsPanel::Invoke(Group& group, LocalModelCommand command) {
  const auto* selected = Selected(group);
  if (!selected || dialog_)
    return;
  const auto& row = *selected;
  if (command == LocalModelCommand::kRemove ||
      !row.flags.Has(LocalModelFlag::kShowFirstUse)) {
    context_.delegate.OnLocalModelCommand(row.kind, row.id, command);
    return;
  }
  controls::ContentDialog dialog;
  dialog.XamlRoot(root_.XamlRoot());
  dialog.RequestedTheme(root_.ActualTheme());
  const auto kind = std::to_underlying(row.kind);
  dialog.Title(winrt::box_value(first_use_titles_[kind]));
  auto text = Wrapped(kTokens.settings.body_size);
  const auto& notice = first_use_text_[kind];
  auto message = std::wstring(notice.c_str(), notice.size());
  if (command == LocalModelCommand::kDownload) {
    message += L"\n\n";
    message +=
        winrt::unbox_value<winrt::hstring>(group.transfer.Content()).c_str();
  }
  text.Text(winrt::hstring(message));
  dialog.Content(text);
  dialog.PrimaryButtonText(continue_);
  dialog.CloseButtonText(cancel_);
  dialog.DefaultButton(controls::ContentDialogButton::Close);
  dialog_ = dialog;
  const auto show = [](LocalModelsPanel* owner, std::weak_ptr<int> lifetime,
                       controls::ContentDialog retained, LocalModelKind kind,
                       std::uint32_t id,
                       LocalModelCommand command) -> winrt::fire_and_forget {
    controls::ContentDialogResult result = controls::ContentDialogResult::None;
    try {
      result = co_await retained.ShowAsync();
    } catch (...) {
      platform::WriteDefaultLog(LogLevel::kWarning,
                                "Model settings dialog could not open");
    }
    if (lifetime.expired() || owner->dialog_ != retained)
      co_return;
    owner->dialog_ = nullptr;
    if (result == controls::ContentDialogResult::Primary)
      owner->context_.delegate.OnLocalModelCommand(kind, id, command);
  };
  show(this, lifetime_, std::move(dialog), row.kind, row.id, command);
}

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
      style_.Dropdown(r.choice);
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
      r.control = style_.Toggle(r.toggle);
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
  if (r.toggle)
    xaml::Automation::AutomationProperties::SetLabeledBy(r.toggle, r.title);
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
  const auto& row = *found->second;
  return row.toggle ? row.toggle.Focus(xaml::FocusState::Programmatic)
                    : row.control.Focus(xaml::FocusState::Programmatic);
}
}  // namespace kr::win
