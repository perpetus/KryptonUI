#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/core/ids.hpp"
#include "krypton/ui/core/result.hpp"

namespace kr {
using TaskId = Id<struct TaskTag>;
using PermissionId = Id<struct PermissionTag>;
enum class TaskCategory : std::uint32_t { kTab, kExtension, kBrowser };
enum class TaskFilter : std::uint32_t { kTabsAndExtensions, kBrowser, kAll };
enum class TaskColumn : std::uint32_t {
  kName,
  kMemory,
  kCpu,
  kNetwork,
  kProcess
};
struct BrowserTask {
  TaskId id;
  std::uint32_t process_id = 0;
  TaskCategory category = TaskCategory::kTab;
  std::string title, profile;
  ImageId icon;
  std::uint64_t memory_bytes = 0, network_bytes_per_second = 0;
  double cpu_percent = -1;
  bool can_end = false;
};
enum class PermissionKind : std::uint32_t {
  kOther,
  kLocation,
  kCamera,
  kMicrophone,
  kNotifications,
  kClipboard,
  kPopups,
  kSound,
  kAutomaticDownloads,
  kUsb,
  kBluetooth,
  kSerial
};
enum class PermissionSetting : std::uint32_t {
  kDefault,
  kAllow,
  kBlock,
  kAsk,
  kSession
};
enum class PermissionDecision : std::uint32_t {
  kAllowOnce,
  kAllowAlways,
  kBlock,
  kDismiss
};
struct SitePermission {
  PermissionId id;
  std::string origin, label, description;
  PermissionKind kind = PermissionKind::kOther;
  PermissionSetting setting = PermissionSetting::kDefault;
  std::uint32_t allowed_settings = 0x0f;
  bool managed = false;
};
struct PermissionRequest {
  PermissionKind kind = PermissionKind::kOther;
  std::string label;
};
struct PermissionPrompt {
  std::uint64_t id = 0;
  std::string origin, embedding_origin, message;
  std::vector<PermissionRequest> requests;
  Rect anchor;
  bool allow_once = false, allow_always = true;
};

// Host data and UI-local choices live together in the Store. Revisions let
// presenters and native surfaces skip unrelated metric/permission updates.
class BrowserToolsState {
 public:
  [[nodiscard]] Result<void> UpsertTask(BrowserTask value);
  [[nodiscard]] Result<void> RemoveTask(TaskId id);
  [[nodiscard]] Result<void> UpsertPermission(SitePermission value);
  [[nodiscard]] Result<void> RemovePermission(PermissionId id);
  [[nodiscard]] Result<void> SetPrompt(PermissionPrompt value);
  void DismissPrompt(std::uint64_t id);
  void OpenTasks();
  void CloseTasks();
  void OpenPermissions(std::string origin);
  void ClosePermissions();
  [[nodiscard]] const BrowserTask* FindTask(TaskId id) const noexcept;
  [[nodiscard]] const SitePermission* FindPermission(
      PermissionId id) const noexcept;
  [[nodiscard]] bool CanEndSelection() const noexcept;
  void SelectTasks(std::span<const TaskId> ids);
  void SetTaskSearch(std::string text);
  void SetTaskFilter(TaskFilter value);
  void SortTasks(TaskColumn column);
  void SetPermissionSearch(std::string text);

  std::vector<BrowserTask> tasks;
  std::vector<SitePermission> permissions;
  std::vector<TaskId> selected_tasks;
  std::optional<PermissionPrompt> prompt;
  std::string task_search, permission_search, permission_origin;
  std::string task_error, permission_error;
  TaskFilter task_filter = TaskFilter::kTabsAndExtensions;
  TaskColumn task_sort = TaskColumn::kName;
  bool ascending = true;
  bool tasks_open = false, permissions_open = false;
  bool can_end_tasks = false, can_activate_tasks = false;
  bool can_edit_permissions = false, can_reset_permissions = false;
  std::uint64_t task_revision = 0, permission_revision = 0, prompt_revision = 0;
  std::uint32_t task_serial = 0, permission_serial = 0;

 private:
  std::unordered_map<TaskId, std::size_t> task_index_;
  std::unordered_map<PermissionId, std::size_t> permission_index_;
};

[[nodiscard]] bool ToolSearchMatches(std::string_view text,
                                     std::string_view query) noexcept;
[[nodiscard]] bool TaskVisible(const BrowserTask& task,
                               TaskFilter filter,
                               std::string_view search) noexcept;
[[nodiscard]] bool PermissionSettingAllowed(const SitePermission& item,
                                            PermissionSetting setting) noexcept;
}  // namespace kr
