#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

#include <algorithm>

#include "krypton/ui/presentation/folder_layout.hpp"
#include "krypton/ui/presentation/folder_menu.hpp"
#include "live_folder_editor.hpp"
#include "live_folder_icon.hpp"
#include "tab_list.hpp"

namespace kr::win {
using xaml::Thickness;
using xaml::Visibility;
void TabList::UpdateFolderRow(FolderRecord& record) {
  if (!record.row)
    return;
  const auto value = context_.delegate.OnFolderRequested(record.id);
  if (!value)
    return;
  auto& row = *record.row;
  const bool sign_in = LiveFolderNeedsSignIn(*value);
  const auto feedback = PresentLiveFolderFeedback(*value);
  if (rendered_ && row.folder == record.id && row.depth == record.depth &&
      row.data.title == value->name && row.data.url == value->glyph &&
      row.folder_collapsed == value->collapsed &&
      row.folder_live_kind == value->live_kind &&
      row.folder_sign_in_required == sign_in &&
      row.folder_live_status == value->live_status)
    return;
  if (row.data.title != value->name ||
      row.folder_live_kind != value->live_kind || row.data.title_dirty) {
    row.data.title = value->name;
    const auto default_name = LiveFolderDefaultName(*value);
    row.data.native_title = default_name ? NativeText(*default_name)
                                         : winrt::to_hstring(value->name);
    row.data.title_dirty = false;
  }
  const bool animate = row.folder == record.id;
  row.data.essential = false;
  row.background.BorderBrush(context_.brushes.transparent);
  row.background.BorderThickness(UniformThickness(0));
  if (row.folder != record.id)
    row.configured = false;
  row.data.url = value->glyph;
  row.folder_collapsed = value->collapsed;
  row.folder_live_kind = value->live_kind;
  row.folder_sign_in_required = sign_in;
  row.folder_live_status = value->live_status;
  row.folder = record.id;
  row.depth = record.depth;
  row.active = false;
  row.data.active = row.data.audible = row.data.muted = false;
  ApplyRowContent(row);
  if (row.split)
    row.split->root.Visibility(Visibility::Collapsed);
  row.container.IsEnabled(true);
  row.container.IsTabStop(true);
  ShowClear(row, false);
  row.background.Height(kTokens.spacing.tab_height);
  row.background.Padding(
      Thickness{icon_only_ ? 0 : FolderRowInset(row.depth), 0, 0, 0});
  row.background.Background(context_.brushes.transparent);
  row.divider.Visibility(Visibility::Collapsed);
  if (!row.folder_icon) {
    row.folder_icon = std::make_unique<FolderIcon>();
    row.grid.Children().Append(row.folder_icon->Root());
  }
  auto icon = row.folder_icon->Root();
  icon.HorizontalAlignment(icon_only_ ? xaml::HorizontalAlignment::Center
                                      : xaml::HorizontalAlignment::Left);
  icon.VerticalAlignment(xaml::VerticalAlignment::Center);
  icon.Visibility(Visibility::Visible);
  row.folder_icon->Update(!value->collapsed, record.contains_active,
                          value->glyph, folder_theme_, context_.brushes,
                          context_.motion, animate, value->live_kind);
  row.glyph.Visibility(Visibility::Collapsed);
  row.image.Visibility(Visibility::Collapsed);
  const auto status = winrt::to_hstring(feedback.text);
  row.title.Text(feedback.text.empty()
                     ? row.data.native_title
                     : row.data.native_title + L" · " + status);
  row.title.FontSize(kTokens.type.size);
  row.title.Foreground(context_.brushes.text);
  row.title.Visibility(icon_only_ ? Visibility::Collapsed
                                  : Visibility::Visible);
  row.status.Text(feedback.warning ? L"\uE7BA" : L"");
  row.status.Visibility(feedback.warning && !icon_only_
                            ? Visibility::Visible
                            : Visibility::Collapsed);
  SetToolTip(row.background, status);
  row.close.control.Visibility(Visibility::Collapsed);
  row.audio.control.Visibility(Visibility::Collapsed);
  Accessible(row.background, row.title.Text());
}

void TabList::RenameFolder(TabRow& row) {
  const auto folder = context_.delegate.OnFolderRequested(row.folder);
  if (!folder)
    return;
  row.editing_folder = folder->id;
  if (!row.rename_flyout) {
    row.rename_flyout = controls::Flyout();
    controls::StackPanel content;
    content.Spacing(kTokens.spacing.section_gap);
    row.rename_input = controls::TextBox();
    row.rename_picker = std::make_unique<WorkspaceIconPicker>(context_, true);
    row.rename_save = controls::Button();
    content.Children().Append(row.rename_input);
    content.Children().Append(row.rename_picker->Root());
    content.Children().Append(row.rename_save);
    row.rename_flyout.Content(content);
    row.rename_click = row.rename_save.Click(
        winrt::auto_revoke, [this, native = &row](auto&&, auto&&) {
          const auto name = winrt::to_string(native->rename_input.Text());
          const auto glyph = native->rename_picker->Value();
          if (context_.delegate.OnFolderCommand(
                  {.kind = FolderCommandKind::kRename,
                   .folder = native->editing_folder,
                   .name = name,
                   .glyph = glyph}))
            native->rename_flyout.Hide();
        });
  }
  row.rename_save.Content(winrt::box_value(folder_text_[7]));
  row.rename_input.Header(winrt::box_value(folder_text_[3]));
  row.rename_input.Text(winrt::to_hstring(folder->name));
  auto labels = picker_labels_;
  labels[0] = folder_text_[8];
  row.rename_picker->SetLabels(labels);
  row.rename_picker->SetValue(folder->glyph);
  row.rename_input.SelectAll();
  row.rename_flyout.ShowAt(row.background);
}

void TabList::ShowFolderMenu(TabRow& row) {
  DismissHover();
  const auto folder = context_.delegate.OnFolderRequested(row.folder);
  if (!folder)
    return;
  const auto folder_id = row.folder;
  row.folder_clicks.clear();
  row.folder_menu = controls::MenuFlyout();
  row.folder_menu.AreOpenCloseAnimationsEnabled(false);
  const auto add = [&](const auto& items, winrt::hstring text,
                       FolderCommandKind kind,
                       std::optional<LiveFolderProvider> preset =
                           std::nullopt) {
    controls::MenuFlyoutItem item;
    item.Text(text);
    row.folder_clicks.push_back(item.Click(
        winrt::auto_revoke,
        [this, native = &row, folder_id, kind, preset](auto&&, auto&&) {
          if (native->folder != folder_id)
            return;
          if (kind == FolderCommandKind::kSetLive && preset &&
              IsGitHubLiveFolder(*preset)) {
            const auto current = context_.delegate.OnFolderRequested(folder_id);
            if (!current || !current->live_supported)
              return;
            const auto draft = PresentLiveFolderDraft(*current, preset);
            (void)context_.delegate.OnFolderCommand(
                {.kind = kind,
                 .folder = folder_id,
                 .live_kind = std::to_underlying(draft.provider),
                 .live_source = draft.source,
                 .live_interval_minutes = draft.interval_minutes,
                 .live_max_items = draft.max_items});
            return;
          }
          if (kind == FolderCommandKind::kSetLive) {
            const auto id = native->folder;
            const auto anchor = winrt::make_weak(native->background);
            const auto lifetime = std::weak_ptr(lifetime_);
            native->folder_menu.Hide();
            (void)native->background.DispatcherQueue().TryEnqueue(
                [this, lifetime, anchor, id, preset] {
                  if (lifetime.expired())
                    return;
                  const auto target = anchor.get();
                  const auto folder = context_.delegate.OnFolderRequested(id);
                  if (!target || !folder || !folder->live_supported)
                    return;
                  if (!live_folder_editor_)
                    live_folder_editor_ =
                        std::make_unique<LiveFolderEditor>(context_);
                  live_folder_editor_->Show(target, *folder, preset);
                });
            return;
          }
          if (kind == FolderCommandKind::kRename) {
            RenameFolder(*native);
            return;
          }
          FolderCommand command{.kind = kind, .folder = native->folder};
          if (kind == FolderCommandKind::kCreate) {
            command.parent = native->folder;
            command.folder = {};
            command.name = "Folder";
          }
          (void)context_.delegate.OnFolderCommand(command);
        }));
    items.Append(item);
    return item;
  };
  const auto label_for = [&](UiText label) {
    const auto at = std::ranges::find(kFolderTextKeys, label);
    return at == kFolderTextKeys.end() ? NativeText(label)
                                       : folder_text_[static_cast<std::size_t>(
                                             at - kFolderTextKeys.begin())];
  };
  controls::MenuFlyoutSubItem live, move;
  live.Text(label_for(UiText::kLiveFolderMenu));
  live.Icon(LiveFolderMenuIcon(
      folder->live_kind == 0
          ? LiveFolderProvider::kRss
          : static_cast<LiveFolderProvider>(folder->live_kind)));
  if (LiveFolderNeedsSignIn(*folder)) {
    controls::MenuFlyoutItem warning;
    warning.Text(NativeText(UiText::kLiveFolderSignedOut));
    warning.IsEnabled(false);
    live.Items().Append(warning);
    controls::MenuFlyoutItem sign_in;
    sign_in.Text(NativeText(UiText::kLiveFolderSignIn));
    sign_in.Icon(LiveFolderMenuIcon(LiveFolderProvider::kPullRequests));
    row.folder_clicks.push_back(sign_in.Click(
        winrt::auto_revoke, [this, native = &row, folder_id](auto&&, auto&&) {
          if (native->folder != folder_id)
            return;
          const auto lifetime = std::weak_ptr(lifetime_);
          const auto queue = native->background.DispatcherQueue();
          native->folder_menu.Hide();
          (void)queue.TryEnqueue([this, lifetime, folder_id] {
            if (!lifetime.expired() &&
                context_.delegate.OnFolderRequested(folder_id))
              context_.delegate.OnUrlDropped("https://github.com/login");
          });
        }));
    live.Items().Append(sign_in);
    live.Items().Append(controls::MenuFlyoutSeparator());
  }
  live.IsEnabled(folder->live_supported);
  move.Text(label_for(UiText::kMoveFolderMenu));
  bool grouped = false;
  for (const auto& action : PresentFolderMenu(*folder)) {
    if (action.section != FolderMenuSection::kMain && !grouped) {
      row.folder_menu.Items().Append(live);
      row.folder_menu.Items().Append(move);
      grouped = true;
    }
    const auto items = action.section == FolderMenuSection::kLive ? live.Items()
                       : action.section == FolderMenuSection::kMove
                           ? move.Items()
                           : row.folder_menu.Items();
    if (action.separator)
      items.Append(controls::MenuFlyoutSeparator());
    const auto text = label_for(action.label);
    if (!action.workspaces) {
      const auto item = add(items, text, action.command, action.live_provider);
      item.IsEnabled(action.enabled);
      if (action.live_provider)
        item.Icon(LiveFolderMenuIcon(*action.live_provider));
      continue;
    }
    controls::MenuFlyoutSubItem workspaces;
    workspaces.Text(text);
    for (const auto& [id, name] : folder_workspaces_) {
      controls::MenuFlyoutItem item;
      item.Text(name);
      row.folder_clicks.push_back(
          item.Click(winrt::auto_revoke,
                     [this, native = &row, folder_id, id](auto&&, auto&&) {
                       if (native->folder != folder_id)
                         return;
                       (void)context_.delegate.OnFolderCommand(
                           {.kind = FolderCommandKind::kMove,
                            .folder = native->folder,
                            .space = id});
                     }));
      workspaces.Items().Append(item);
    }
    items.Append(workspaces);
  }
  const auto provider = static_cast<LiveFolderProvider>(folder->live_kind);
  if (IsGitHubLiveFolder(provider)) {
    controls::MenuFlyoutSubItem filters;
    filters.Text(NativeText(UiText::kLiveFolderShow));
    for (const auto& filter : kLiveFolderGitHubFilters) {
      if (filter.pulls_only && provider != LiveFolderProvider::kPullRequests)
        continue;
      controls::ToggleMenuFlyoutItem item;
      item.Text(NativeText(filter.label));
      item.IsChecked(folder->live_source == filter.source);
      row.folder_clicks.push_back(item.Click(
          winrt::auto_revoke,
          [this, native = &row, folder_id, provider, filter](auto&&, auto&&) {
            if (native->folder != folder_id)
              return;
            const auto current = context_.delegate.OnFolderRequested(folder_id);
            if (!current || !current->live_supported ||
                current->live_kind != std::to_underlying(provider))
              return;
            (void)context_.delegate.OnFolderCommand(
                {.kind = FolderCommandKind::kSetLive,
                 .folder = folder_id,
                 .live_kind = current->live_kind,
                 .live_source = filter.source,
                 .live_interval_minutes = current->live_interval_minutes,
                 .live_max_items = current->live_max_items});
          }));
      filters.Items().Append(item);
    }
    live.Items().Append(controls::MenuFlyoutSeparator());
    live.Items().Append(filters);
  }
  row.folder_menu.ShowAt(row.background);
}
}  // namespace kr::win
