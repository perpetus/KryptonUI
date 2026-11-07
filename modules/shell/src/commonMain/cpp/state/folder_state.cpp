#include <algorithm>

#include "krypton/ui/state/store.hpp"

namespace kr {
const Folder* Store::FindFolder(FolderId id) const noexcept {
  const auto found = folder_index_.find(id);
  return found == folder_index_.end() ? nullptr : &folders_[found->second];
}
Result<void> Store::UpsertFolder(Folder folder) {
  if (!folder.id || !folder.space || folder.name.empty() ||
      folder.name.find_first_not_of(" \t\r\n") == std::string::npos ||
      folder.name.find('\0') != std::string::npos ||
      folder.glyph.find('\0') != std::string::npos)
    return Fail(ErrorCode::kInvalidArgument, "Invalid folder");
  const auto candidate = [&](FolderId id) -> const Folder* {
    return id == folder.id ? &folder : FindFolder(id);
  };
  // Validate existing descendants as well as the moved folder. This bounds
  // every ancestor walk and rejects cycles before changing the owned graph.
  const auto valid = [&](const Folder& item) {
    const Folder* current = &item;
    for (std::size_t depth = 1; depth <= kMaxFolderDepth; ++depth) {
      if (!current->parent)
        return true;
      current = candidate(current->parent);
      if (!current || current->space != item.space)
        return false;
    }
    return false;
  };
  if (!valid(folder))
    return Fail(ErrorCode::kInvalidArgument, "Invalid folder hierarchy");
  for (const auto& existing : folders_)
    if (existing.id != folder.id && !valid(existing))
      return Fail(ErrorCode::kInvalidArgument, "Invalid descendant hierarchy");
  const auto found = folder_index_.find(folder.id);
  if (found == folder_index_.end()) {
    folder_index_.emplace(folder.id, folders_.size());
    folders_.push_back(std::move(folder));
  } else {
    folders_[found->second] = std::move(folder);
  }
  MarkDirty(Dirty::kTabs);
  return {};
}
Result<void> Store::RemoveFolder(FolderId id) {
  if (!FindFolder(id))
    return Fail(ErrorCode::kNotFound, "Unknown folder");
  const auto removed = [&](FolderId item) {
    for (std::size_t depth = 0; item && depth < kMaxFolderDepth; ++depth) {
      if (item == id)
        return true;
      const auto* folder = FindFolder(item);
      item = folder ? folder->parent : FolderId{};
    }
    return false;
  };
  for (auto& tab : tabs_)
    if (removed(tab.folder))
      tab.folder = {};
  std::vector<FolderId> ids;
  for (const auto& folder : folders_)
    if (removed(folder.id))
      ids.push_back(folder.id);
  std::erase_if(folders_, [&](const Folder& folder) {
    return std::ranges::find(ids, folder.id) != ids.end();
  });
  folder_index_.clear();
  for (std::size_t i = 0; i < folders_.size(); ++i)
    folder_index_.emplace(folders_[i].id, i);
  MarkDirty(Dirty::kTabs);
  return {};
}
}  // namespace kr
