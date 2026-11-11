#pragma once

#include <array>
#include <span>
#include <string_view>

#include "krypton/ui/model/browser_tools.hpp"

namespace kr {
inline constexpr std::array<std::string_view, 5> kTaskColumnLabels{
    "Task", "Memory footprint", "CPU", "Network", "Process ID"};
inline constexpr std::array<std::string_view, 3> kTaskFilterLabels{
    "Tabs and extensions", "Browser", "All"};
inline constexpr std::array<std::string_view, 5> kPermissionSettingLabels{
    "Use default", "Allow", "Block", "Ask", "Allow for this session"};
inline constexpr std::string_view kTaskManagerTitle = "Task Manager";
inline constexpr std::string_view kPermissionManagerTitle = "Site permissions";
inline constexpr std::string_view kCloseSiteInfoLabel =
    "Close site information";
inline constexpr std::string_view kTaskSearchPlaceholder = "Search tasks";
inline constexpr std::string_view kPermissionSearchPlaceholder =
    "Search sites or permissions";
inline constexpr std::string_view kEndProcessLabel = "End process";
inline constexpr std::string_view kResetPermissionsLabel = "Reset permissions";
inline constexpr std::string_view kPermissionPromptTitle = "wants to";
inline constexpr std::string_view kPermissionAllowOnceLabel = "Allow this time";
inline constexpr std::string_view kPermissionAllowAlwaysLabel =
    "Allow on every visit";
inline constexpr std::string_view kPermissionBlockLabel = "Don't allow";
inline constexpr std::string_view kTaskCategoryLabel = "Task category";
inline constexpr std::string_view kBrowserTasksLabel = "Browser tasks";
inline constexpr std::string_view kNoTasksLabel = "No matching tasks";
inline constexpr std::string_view kNoPermissionsLabel =
    "No matching site permissions";
inline constexpr std::string_view kClosePermissionPromptLabel =
    "Close permission request";
inline constexpr std::string_view kEmbeddedOriginLabel = "Embedded in";
struct BrowserTaskVm {
  TaskId id;
  std::string_view title, profile, memory, cpu, network, process;
  ImageId icon;
  TaskCategory category = TaskCategory::kTab;
  bool selected = false;
};
struct SitePermissionVm {
  PermissionId id;
  std::string_view origin, label, description;
  PermissionKind kind = PermissionKind::kOther;
  PermissionSetting setting = PermissionSetting::kDefault;
  std::array<PermissionSetting, 5> choices{};
  std::size_t choice_count = 0;
  bool editable = false, managed = false;
};
struct BrowserToolsVm {
  bool tasks_open = false, permissions_open = false, can_end = false,
       can_reset = false;
  std::uint32_t task_serial = 0, permission_serial = 0;
  std::uint64_t task_revision = 0, permission_revision = 0, prompt_revision = 0;
  TaskFilter filter = TaskFilter::kTabsAndExtensions;
  TaskColumn sort = TaskColumn::kName;
  bool ascending = true;
  std::string_view task_search, permission_search, origin, task_status,
      permission_status;
  std::string_view task_error, permission_error;
  std::span<const BrowserTaskVm> tasks;
  std::span<const SitePermissionVm> permissions;
  const PermissionPrompt* prompt = nullptr;
};
}  // namespace kr
