#include "krypton/ui/model/browser_tools.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kr {
namespace {
constexpr unsigned char Fold(unsigned char value) noexcept {
  return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}
}  // namespace
bool ToolSearchMatches(std::string_view text, std::string_view query) noexcept {
  return std::search(text.begin(), text.end(), query.begin(), query.end(),
                     [](unsigned char a, unsigned char b) {
                       return Fold(a) == Fold(b);
                     }) != text.end() ||
         query.empty();
}
bool TaskVisible(const BrowserTask& task,
                 TaskFilter filter,
                 std::string_view search) noexcept {
  if (filter == TaskFilter::kBrowser && task.category != TaskCategory::kBrowser)
    return false;
  if (filter == TaskFilter::kTabsAndExtensions &&
      task.category == TaskCategory::kBrowser)
    return false;
  return ToolSearchMatches(task.title, search) ||
         ToolSearchMatches(task.profile, search);
}
bool PermissionSettingAllowed(const SitePermission& item,
                              PermissionSetting setting) noexcept {
  const auto value = std::to_underlying(setting);
  return value <= std::to_underlying(PermissionSetting::kSession) &&
         (item.allowed_settings & (1u << value)) != 0;
}
Result<void> BrowserToolsState::UpsertTask(BrowserTask value) {
  const auto id = value.id;
  if (!value.id || value.category > TaskCategory::kBrowser ||
      !std::isfinite(value.cpu_percent) || value.cpu_percent < -1)
    return Fail(ErrorCode::kInvalidArgument, "Invalid browser task");
  if (const auto found = task_index_.find(value.id); found != task_index_.end())
    tasks[found->second] = std::move(value);
  else {
    task_index_.emplace(value.id, tasks.size());
    tasks.push_back(std::move(value));
  }
  if (!TaskVisible(*FindTask(id), task_filter, task_search))
    std::erase(selected_tasks, id);
  ++task_revision;
  return {};
}
Result<void> BrowserToolsState::RemoveTask(TaskId id) {
  const auto found = task_index_.find(id);
  if (found == task_index_.end())
    return Fail(ErrorCode::kNotFound, "Unknown task");
  const auto index = found->second;
  task_index_.erase(found);
  tasks.erase(tasks.begin() + static_cast<std::ptrdiff_t>(index));
  for (std::size_t i = index; i < tasks.size(); ++i)
    task_index_[tasks[i].id] = i;
  std::erase(selected_tasks, id);
  ++task_revision;
  return {};
}
Result<void> BrowserToolsState::UpsertPermission(SitePermission value) {
  if (!value.id || value.origin.empty() ||
      value.kind > PermissionKind::kSerial ||
      value.setting > PermissionSetting::kSession ||
      (!value.managed && !(value.allowed_settings & 0x1f)))
    return Fail(ErrorCode::kInvalidArgument, "Invalid site permission");
  if (const auto found = permission_index_.find(value.id);
      found != permission_index_.end())
    permissions[found->second] = std::move(value);
  else {
    permission_index_.emplace(value.id, permissions.size());
    permissions.push_back(std::move(value));
  }
  ++permission_revision;
  return {};
}
Result<void> BrowserToolsState::RemovePermission(PermissionId id) {
  const auto found = permission_index_.find(id);
  if (found == permission_index_.end())
    return Fail(ErrorCode::kNotFound, "Unknown permission");
  const auto index = found->second;
  permission_index_.erase(found);
  permissions.erase(permissions.begin() + static_cast<std::ptrdiff_t>(index));
  for (std::size_t i = index; i < permissions.size(); ++i)
    permission_index_[permissions[i].id] = i;
  ++permission_revision;
  return {};
}
Result<void> BrowserToolsState::SetPrompt(PermissionPrompt value) {
  if (!value.id || value.origin.empty() || value.requests.empty() ||
      value.requests.size() > 16 ||
      (!value.allow_once && !value.allow_always) ||
      !std::isfinite(value.anchor.x) || !std::isfinite(value.anchor.y) ||
      !std::isfinite(value.anchor.width) ||
      !std::isfinite(value.anchor.height) || value.anchor.width < 0 ||
      value.anchor.height < 0)
    return Fail(ErrorCode::kInvalidArgument, "Invalid permission prompt");
  for (const auto& request : value.requests)
    if (request.kind > PermissionKind::kSerial || request.label.empty())
      return Fail(ErrorCode::kInvalidArgument, "Invalid permission request");
  prompt = std::move(value);
  ++prompt_revision;
  return {};
}
void BrowserToolsState::DismissPrompt(std::uint64_t id) {
  if (prompt && prompt->id == id) {
    prompt.reset();
    ++prompt_revision;
  }
}
void BrowserToolsState::OpenTasks() {
  tasks_open = true;
  ++task_serial;
  ++task_revision;
  task_error.clear();
}
void BrowserToolsState::CloseTasks() {
  tasks_open = false;
  selected_tasks.clear();
  ++task_revision;
}
void BrowserToolsState::OpenPermissions(std::string origin) {
  permissions_open = true;
  permission_origin = std::move(origin);
  permission_error.clear();
  ++permission_serial;
  ++permission_revision;
}
void BrowserToolsState::ClosePermissions() {
  permissions_open = false;
  ++permission_revision;
}
const BrowserTask* BrowserToolsState::FindTask(TaskId id) const noexcept {
  const auto found = task_index_.find(id);
  return found == task_index_.end() ? nullptr : &tasks[found->second];
}
const SitePermission* BrowserToolsState::FindPermission(
    PermissionId id) const noexcept {
  const auto found = permission_index_.find(id);
  return found == permission_index_.end() ? nullptr
                                          : &permissions[found->second];
}
bool BrowserToolsState::CanEndSelection() const noexcept {
  if (!can_end_tasks || selected_tasks.empty())
    return false;
  return std::ranges::all_of(selected_tasks, [this](TaskId id) {
    const auto* task = FindTask(id);
    return task && task->can_end &&
           TaskVisible(*task, task_filter, task_search);
  });
}
void BrowserToolsState::SelectTasks(std::span<const TaskId> ids) {
  selected_tasks.clear();
  for (const auto id : ids)
    if (const auto* task = FindTask(id);
        task && TaskVisible(*task, task_filter, task_search))
      selected_tasks.push_back(id);
  std::ranges::sort(selected_tasks);
  selected_tasks.erase(
      std::unique(selected_tasks.begin(), selected_tasks.end()),
      selected_tasks.end());
  ++task_revision;
}
void BrowserToolsState::SetTaskSearch(std::string text) {
  task_search = std::move(text);
  selected_tasks.clear();
  ++task_revision;
}
void BrowserToolsState::SetTaskFilter(TaskFilter value) {
  if (value > TaskFilter::kAll)
    return;
  task_filter = value;
  selected_tasks.clear();
  ++task_revision;
}
void BrowserToolsState::SortTasks(TaskColumn column) {
  if (column > TaskColumn::kProcess)
    return;
  ascending = task_sort == column ? !ascending : column == TaskColumn::kName;
  task_sort = column;
  ++task_revision;
}
void BrowserToolsState::SetPermissionSearch(std::string text) {
  permission_search = std::move(text);
  ++permission_revision;
}
}  // namespace kr
