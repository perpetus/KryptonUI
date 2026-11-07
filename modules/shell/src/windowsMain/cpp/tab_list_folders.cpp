#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

#include <algorithm>
#include <bit>

#include "krypton/ui/presentation/folder_menu.hpp"
#include "tab_list.hpp"

namespace kr::win {
using xaml::Thickness;
using xaml::Visibility;
void TabList::ReportFolderDrop(FolderId id) {
  const auto folder = context_.delegate.OnFolderRequested(id);
  const auto record = folder_records_.find(id);
  if (!folder || record == folder_records_.end())
    return;
  std::uint32_t position = 0;
  if (!items_.IndexOf(record->second.item, position))
    return;
  std::size_t order = 0;
  for (std::uint32_t i = 0; i < position; ++i) {
    const auto item = items_.GetAt(i).as<foundation::IPropertyValue>();
    if (item.Type() != foundation::PropertyType::Int64)
      continue;
    const auto other = context_.delegate.OnFolderRequested(
        FolderId{std::bit_cast<std::uint64_t>(item.GetInt64())});
    if (other && other->parent == folder->parent)
      ++order;
  }
  (void)context_.delegate.OnFolderCommand({.kind = FolderCommandKind::kMove,
                                           .folder = id,
                                           .parent = folder->parent,
                                           .index = order});
}
void TabList::UpdateFolderRow(FolderRecord& record) {
  if (!record.row)
    return;
  const auto value = context_.delegate.OnFolderRequested(record.id);
  if (!value)
    return;
  auto& row = *record.row;
  if (rendered_ && row.folder == record.id && row.depth == record.depth &&
      row.data.title == value->name && row.data.url == value->glyph &&
      row.folder_collapsed == value->collapsed)
    return;
  if (row.data.title != value->name || row.data.title_dirty) {
    row.data.title = value->name;
    row.data.native_title = winrt::to_hstring(value->name);
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
  row.background.Padding(Thickness{
      icon_only_
          ? 0
          : kTokens.spacing.tab_padding * static_cast<float>(row.depth + 1),
      0, 0, 0});
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
                          context_.motion, animate);
  row.glyph.Visibility(Visibility::Collapsed);
  row.image.Visibility(Visibility::Collapsed);
  row.title.Text(row.data.native_title);
  row.title.FontSize(kTokens.type.size);
  row.title.Foreground(context_.brushes.text);
  row.title.Visibility(icon_only_ ? Visibility::Collapsed
                                  : Visibility::Visible);
  row.status.Visibility(Visibility::Collapsed);
  row.close.control.Visibility(Visibility::Collapsed);
  row.audio.control.Visibility(Visibility::Collapsed);
  Accessible(row.background, winrt::to_hstring(value->name));
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
  row.folder_clicks.clear();
  row.folder_menu = controls::MenuFlyout();
  const auto add = [&](winrt::hstring text, FolderCommandKind kind) {
    controls::MenuFlyoutItem item;
    item.Text(text);
    row.folder_clicks.push_back(item.Click(
        winrt::auto_revoke, [this, native = &row, kind](auto&&, auto&&) {
          if (!native->folder)
            return;
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
    row.folder_menu.Items().Append(item);
    return item;
  };
  const auto folder = context_.delegate.OnFolderRequested(row.folder);
  if (!folder)
    return;
  for (const auto& action : PresentFolderMenu(*folder)) {
    if (action.separator)
      row.folder_menu.Items().Append(controls::MenuFlyoutSeparator());
    const auto label = std::ranges::find(kFolderTextKeys, action.label);
    const auto text = label == kFolderTextKeys.end()
                          ? NativeText(action.label)
                          : folder_text_[static_cast<std::size_t>(
                                label - kFolderTextKeys.begin())];
    if (!action.workspaces) {
      add(text, action.command).IsEnabled(action.enabled);
      continue;
    }
    controls::MenuFlyoutSubItem workspaces;
    workspaces.Text(text);
    for (const auto& [id, name] : folder_workspaces_) {
      controls::MenuFlyoutItem item;
      item.Text(name);
      row.folder_clicks.push_back(item.Click(
          winrt::auto_revoke, [this, native = &row, id](auto&&, auto&&) {
            (void)context_.delegate.OnFolderCommand(
                {.kind = FolderCommandKind::kMove,
                 .folder = native->folder,
                 .space = id});
          }));
      workspaces.Items().Append(item);
    }
    row.folder_menu.Items().Append(workspaces);
  }
  row.folder_menu.ShowAt(row.background);
}
}  // namespace kr::win
