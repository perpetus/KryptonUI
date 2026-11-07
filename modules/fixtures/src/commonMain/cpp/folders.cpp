#include <algorithm>
#include <limits>

#include "krypton/fixtures/mock_browser.hpp"

namespace kr::fixtures {
void MockBrowser::PushFolders() const {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, upsert_folder) ||
      !ui_.upsert_folder)
    return;
  for (const auto& folder : folders_) {
    const KrFolder value{sizeof(value),
                         folder.id,
                         folder.space,
                         folder.parent,
                         {folder.name.data(), folder.name.size()},
                         {folder.glyph.data(), folder.glyph.size()},
                         folder.order,
                         folder.collapsed};
    (void)ui_.upsert_folder(ui_.self, &value);
  }
}
KrResult MockBrowser::ExecuteFolderCommand(const KrFolderCommand& command) {
  if (!KR_HAS_FIELD(&command, KrFolderCommand, collapsed) ||
      command.kind > KR_FOLDER_NEW_TAB ||
      (!command.name.data && command.name.size) ||
      (!command.glyph.data && command.glyph.size))
    return KR_ERR_INVALID_ARGUMENT;
  const auto find = [&](KrFolderId id) -> const MockFolder* {
    const auto folder = std::ranges::find(folders_, id, &MockFolder::id);
    return folder == folders_.end() ? nullptr : &*folder;
  };
  const auto descendant = [&](KrFolderId id, KrFolderId parent) {
    for (unsigned depth = 0; id && depth < 5; ++depth) {
      if (id == parent)
        return true;
      const auto* folder = find(id);
      id = folder ? folder->parent : 0;
    }
    return false;
  };
  auto found = std::ranges::find(folders_, command.folder, &MockFolder::id);
  if (command.kind != KR_FOLDER_CREATE && command.kind != KR_FOLDER_MOVE_TAB &&
      found == folders_.end())
    return KR_ERR_INVALID_ARGUMENT;
  if (command.kind == KR_FOLDER_MOVE_TAB || command.kind == KR_FOLDER_NEW_TAB) {
    if (command.folder && !find(command.folder))
      return KR_ERR_INVALID_ARGUMENT;
    KrTabId id = command.tab;
    if (command.kind == KR_FOLDER_NEW_TAB) {
      OpenTab("", KR_OPEN_NEW_TAB);
      id = active_tab_;
    }
    auto* tab = Find(id);
    if (!tab)
      return KR_ERR_INVALID_ARGUMENT;
    tab->folder = command.folder;
    if (const auto* folder = find(command.folder)) {
      tab->space = folder->space;
      tab->flags = (tab->flags | KR_TAB_PINNED) & ~KR_TAB_FAVORITE;
    }
    PushTab(*tab);
    return KR_OK;
  }
  if (command.kind == KR_FOLDER_DELETE || command.kind == KR_FOLDER_UNPACK) {
    std::vector<KrTabId> close;
    for (auto& tab : tabs_)
      if (descendant(tab.folder, command.folder)) {
        tab.folder = 0;
        PushTab(tab);
        if (command.kind == KR_FOLDER_DELETE)
          close.push_back(tab.id);
      }
    std::vector<KrFolderId> removed;
    for (const auto& folder : folders_)
      if (descendant(folder.id, command.folder))
        removed.push_back(folder.id);
    std::erase_if(folders_, [&](const MockFolder& folder) {
      return std::ranges::find(removed, folder.id) != removed.end();
    });
    if (attached_ && KR_HAS_SLOT(&ui_, KrShellUI, remove_folder) &&
        ui_.remove_folder)
      (void)ui_.remove_folder(ui_.self, command.folder);
    for (const auto id : close)
      Close(id);
    return KR_OK;
  }
  auto candidate = folders_;
  if (command.kind == KR_FOLDER_CREATE) {
    if (next_folder_id_ == std::numeric_limits<KrFolderId>::max())
      return KR_ERR_INVALID_ARGUMENT;
    const auto* parent = find(command.parent);
    if (command.parent && !parent)
      return KR_ERR_INVALID_ARGUMENT;
    candidate.push_back(
        {next_folder_id_,
         parent          ? parent->space
         : command.space ? command.space
                         : active_space_,
         command.parent,
         command.name.size ? std::string(command.name.data, command.name.size)
                           : "Folder",
         command.glyph.size
             ? std::string(command.glyph.data, command.glyph.size)
             : "",
         folders_.size(), false});
  } else {
    auto& folder =
        candidate[static_cast<std::size_t>(found - folders_.begin())];
    if (command.kind == KR_FOLDER_RENAME) {
      folder.name = command.name.size
                        ? std::string(command.name.data, command.name.size)
                        : "";
      folder.glyph = command.glyph.size
                         ? std::string(command.glyph.data, command.glyph.size)
                         : "";
    } else if (command.kind == KR_FOLDER_COLLAPSE) {
      folder.collapsed = command.collapsed;
    } else if (command.kind == KR_FOLDER_MOVE) {
      const auto* parent = find(command.parent);
      const auto space = parent          ? parent->space
                         : command.space ? command.space
                                         : folder.space;
      for (auto& child : candidate)
        if (descendant(child.id, command.folder))
          child.space = space;
      folder.parent = command.parent;
      std::vector<MockFolder*> siblings;
      for (auto& child : candidate)
        if (child.id != folder.id && child.parent == folder.parent &&
            child.space == folder.space)
          siblings.push_back(&child);
      std::ranges::sort(siblings, [](const MockFolder* a, const MockFolder* b) {
        return a->order != b->order ? a->order < b->order : a->id < b->id;
      });
      siblings.insert(
          siblings.begin() + std::min(command.index, siblings.size()), &folder);
      for (std::size_t i = 0; i < siblings.size(); ++i)
        siblings[i]->order = i;
    } else
      return KR_ERR_INVALID_ARGUMENT;
  }
  for (const auto& folder : candidate) {
    if (!folder.id || !folder.space ||
        folder.name.find_first_not_of(" \t\r\n") == std::string::npos ||
        folder.name.find('\0') != std::string::npos ||
        folder.glyph.find('\0') != std::string::npos)
      return KR_ERR_INVALID_ARGUMENT;
    const MockFolder* current = &folder;
    for (unsigned depth = 1; current->parent; ++depth) {
      const auto parent =
          std::ranges::find(candidate, current->parent, &MockFolder::id);
      if (parent == candidate.end() || parent->space != folder.space ||
          depth >= 5)
        return KR_ERR_INVALID_ARGUMENT;
      current = &*parent;
    }
  }
  const bool reset =
      std::ranges::any_of(candidate, [&](const MockFolder& folder) {
        const auto* previous = find(folder.id);
        return previous && previous->space != folder.space;
      });
  if (reset && attached_ && KR_HAS_SLOT(&ui_, KrShellUI, remove_folder) &&
      ui_.remove_folder)
    (void)ui_.remove_folder(ui_.self, command.folder);
  folders_ = std::move(candidate);
  if (command.kind == KR_FOLDER_CREATE)
    ++next_folder_id_;
  PushFolders();
  for (auto& tab : tabs_)
    if (const auto* folder = find(tab.folder);
        folder && tab.space != folder->space) {
      tab.space = folder->space;
      PushTab(tab);
    }
  return KR_OK;
}
}  // namespace kr::fixtures
