#pragma once

#include <array>

#include "krypton/ui/presentation/view_model.hpp"

namespace kr {
inline constexpr std::array kFolderTextKeys{
    UiText::kFolder,           UiText::kNewFolder,
    UiText::kNewSubfolder,     UiText::kRenameFolder,
    UiText::kUnpackFolder,     UiText::kDeleteFolder,
    UiText::kMoveFolderToRoot, UiText::kSaveFolder,
    UiText::kFolderIcon,       UiText::kMoveFolderWorkspace};
struct FolderMenuItemVm {
  UiText label;
  FolderCommandKind command;
  bool enabled = true;
  bool separator = false;
  bool workspaces = false;
};
[[nodiscard]] inline auto PresentFolderMenu(const FolderVm& folder) noexcept {
  return std::array{
      FolderMenuItemVm{UiText::kNewTab, FolderCommandKind::kNewTab},
      FolderMenuItemVm{UiText::kNewSubfolder, FolderCommandKind::kCreate,
                       folder.can_add_child},
      FolderMenuItemVm{UiText::kRenameFolder, FolderCommandKind::kRename},
      FolderMenuItemVm{UiText::kMoveFolderToRoot, FolderCommandKind::kMove},
      FolderMenuItemVm{UiText::kMoveFolderWorkspace, FolderCommandKind::kMove,
                       true, false, true},
      FolderMenuItemVm{UiText::kUnpackFolder, FolderCommandKind::kUnpack, true,
                       true},
      FolderMenuItemVm{UiText::kDeleteFolder, FolderCommandKind::kDelete}};
}
}  // namespace kr
