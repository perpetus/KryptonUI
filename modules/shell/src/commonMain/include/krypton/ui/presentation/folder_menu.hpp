#pragma once

#include <array>
#include <optional>

#include "krypton/ui/presentation/live_folder_editor.hpp"

namespace kr {
inline constexpr std::array kFolderTextKeys{UiText::kFolder,
                                            UiText::kNewFolder,
                                            UiText::kNewSubfolder,
                                            UiText::kRenameFolder,
                                            UiText::kUnpackFolder,
                                            UiText::kDeleteFolder,
                                            UiText::kMoveFolderToRoot,
                                            UiText::kSaveFolder,
                                            UiText::kFolderIcon,
                                            UiText::kMoveFolderWorkspace,
                                            UiText::kLiveFolderSettings,
                                            UiText::kRefreshLiveFolder,
                                            UiText::kRestoreLiveItems,
                                            UiText::kLiveFolderMenu,
                                            UiText::kMoveFolderMenu,
                                            UiText::kLiveFolderPullRequestsMenu,
                                            UiText::kLiveFolderIssuesMenu,
                                            UiText::kLiveFolderRssMenu,
                                            UiText::kLiveFolderSignIn};
enum class FolderMenuSection { kMain, kLive, kMove };
struct FolderMenuItemVm {
  UiText label;
  FolderCommandKind command;
  bool enabled = true;
  bool separator = false;
  bool workspaces = false;
  FolderMenuSection section = FolderMenuSection::kMain;
  std::optional<LiveFolderProvider> live_provider = std::nullopt;
};
[[nodiscard]] inline auto PresentFolderMenu(const FolderVm& folder) noexcept {
  return std::array{
      FolderMenuItemVm{UiText::kNewTab, FolderCommandKind::kNewTab},
      FolderMenuItemVm{UiText::kNewSubfolder, FolderCommandKind::kCreate,
                       folder.can_add_child},
      FolderMenuItemVm{UiText::kRenameFolder, FolderCommandKind::kRename},
      FolderMenuItemVm{UiText::kLiveFolderPullRequestsMenu,
                       FolderCommandKind::kSetLive, folder.live_supported,
                       false, false, FolderMenuSection::kLive,
                       LiveFolderProvider::kPullRequests},
      FolderMenuItemVm{UiText::kLiveFolderIssuesMenu,
                       FolderCommandKind::kSetLive, folder.live_supported,
                       false, false, FolderMenuSection::kLive,
                       LiveFolderProvider::kIssues},
      FolderMenuItemVm{UiText::kLiveFolderRssMenu, FolderCommandKind::kSetLive,
                       folder.live_supported, false, false,
                       FolderMenuSection::kLive, LiveFolderProvider::kRss},
      FolderMenuItemVm{UiText::kLiveFolderSettings, FolderCommandKind::kSetLive,
                       folder.live_supported, true, false,
                       FolderMenuSection::kLive},
      FolderMenuItemVm{UiText::kRefreshLiveFolder,
                       FolderCommandKind::kRefreshLive, folder.live_kind != 0,
                       false, false, FolderMenuSection::kLive},
      FolderMenuItemVm{
          UiText::kRestoreLiveItems, FolderCommandKind::kRestoreLiveItems,
          folder.live_kind != 0, false, false, FolderMenuSection::kLive},
      FolderMenuItemVm{UiText::kMoveFolderToRoot, FolderCommandKind::kMove,
                       static_cast<bool>(folder.parent), false, false,
                       FolderMenuSection::kMove},
      FolderMenuItemVm{UiText::kMoveFolderWorkspace, FolderCommandKind::kMove,
                       true, false, true, FolderMenuSection::kMove},
      FolderMenuItemVm{UiText::kUnpackFolder, FolderCommandKind::kUnpack, true,
                       true},
      FolderMenuItemVm{UiText::kDeleteFolder, FolderCommandKind::kDelete}};
}
}  // namespace kr
