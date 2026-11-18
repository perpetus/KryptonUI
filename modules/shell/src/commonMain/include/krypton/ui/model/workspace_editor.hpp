#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "krypton/ui/core/result.hpp"
#include "krypton/ui/model/model.hpp"

namespace kr {
enum class WorkspaceEditKind : std::uint32_t { kUpsert, kRemove };

[[nodiscard]] Result<Space> PrepareWorkspaceEdit(WorkspaceEditKind kind,
                                                 const Space* existing,
                                                 bool can_remove,
                                                 std::string_view name,
                                                 std::string_view glyph,
                                                 Color accent);
struct WorkspaceEditorState {
  bool open = false;
  bool can_remove = false;
  std::uint32_t serial = 0;
  Space value;
  TabId icon_tab;
};

// Strings borrow from the Store during Render.
struct WorkspaceEditorVm {
  bool open = false;
  bool can_remove = false;
  std::uint32_t serial = 0;
  SpaceId id;
  std::string_view name;
  std::string_view glyph;
  TabId icon_tab;
  // Empty when the host offers no profile choice. Borrowed from the Store.
  std::span<const ProfileChoice> profiles;
  ProfileId profile;  // The Space's profile; empty selects the current one.
  SpaceProfileStatus profile_status = SpaceProfileStatus::kReady;
  bool profile_locked = false;  // Tabs or folders keep the current profile.
};
}  // namespace kr
