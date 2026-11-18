#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "krypton/ui/core/ids.hpp"

namespace kr {
// Zen's default includes the root folder in its five levels.
inline constexpr std::size_t kMaxFolderDepth = 5;
enum class LiveFolderProvider : unsigned { kOff, kRss, kPullRequests, kIssues };
struct Folder {
  FolderId id;
  SpaceId space;
  FolderId parent;
  std::string name;
  std::string glyph;
  std::size_t order = 0;
  bool collapsed = false;
  unsigned live_kind = 0;
  std::string live_source;
  unsigned live_interval_minutes = 30;
  unsigned live_max_items = 20;
  std::string live_status;
  bool live_supported = false;
};
enum class FolderCommandKind : unsigned {
  kCreate,
  kRename,
  kCollapse,
  kMove,
  kMoveTab,
  kUnpack,
  kDelete,
  kNewTab,
  kSetLive,
  kRefreshLive,
  kRestoreLiveItems,
};
struct FolderCommand {
  FolderCommandKind kind = FolderCommandKind::kCreate;
  FolderId folder;
  FolderId parent;
  TabId tab;
  SpaceId space;
  std::size_t index = 0;
  std::string_view name;
  std::string_view glyph;
  bool collapsed = false;
  unsigned live_kind = 0;
  std::string_view live_source;
  unsigned live_interval_minutes = 30;
  unsigned live_max_items = 20;
};
}  // namespace kr
