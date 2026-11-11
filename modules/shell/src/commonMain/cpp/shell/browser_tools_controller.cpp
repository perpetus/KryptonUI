#include <utility>

#include "krypton/ui/shell/shell.hpp"

namespace kr {
namespace {
Result<void> ToolResult(KrResult result, std::string_view message) {
  if (result == KR_OK)
    return {};
  return Fail(result == KR_ERR_UNSUPPORTED ? ErrorCode::kUnsupported
                                           : ErrorCode::kPlatform,
              std::string(message));
}
KrStr Borrow(std::string_view text) {
  return {text.data(), text.size()};
}
}  // namespace
bool BrowserPort::CanActivateTasks() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, activate_task) && table_.activate_task;
}
bool BrowserPort::CanEndTasks() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, end_tasks) && table_.end_tasks;
}
bool BrowserPort::CanEditSitePermissions() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, set_site_permission) &&
         table_.set_site_permission;
}
bool BrowserPort::CanResetSitePermissions() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, reset_site_permissions) &&
         table_.reset_site_permissions;
}
bool BrowserPort::CanRespondToPermissionPrompt() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, permission_prompt_response) &&
         table_.permission_prompt_response;
}
void BrowserPort::TaskManagerVisibilityChanged(bool visible) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, task_manager_visibility_changed) &&
      table_.task_manager_visibility_changed)
    table_.task_manager_visibility_changed(table_.self, visible);
}
void BrowserPort::ActivateTask(TaskId id) const {
  if (CanActivateTasks())
    table_.activate_task(table_.self, id.value);
}
Result<void> BrowserPort::EndTasks(std::span<const TaskId> tasks) const {
  if (!CanEndTasks())
    return Fail(ErrorCode::kUnsupported, "Ending processes is unavailable");
  std::vector<KrTaskId> ids;
  ids.reserve(tasks.size());
  for (auto id : tasks)
    ids.push_back(id.value);
  return ToolResult(table_.end_tasks(table_.self, ids.data(), ids.size()),
                    "The browser could not end the selected processes.");
}
void BrowserPort::SitePermissionsVisibilityChanged(std::string_view origin,
                                                   bool visible) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, site_permissions_visibility_changed) &&
      table_.site_permissions_visibility_changed)
    table_.site_permissions_visibility_changed(table_.self, Borrow(origin),
                                               visible);
}
Result<void> BrowserPort::SetSitePermission(PermissionId id,
                                            PermissionSetting setting) const {
  if (!CanEditSitePermissions())
    return Fail(ErrorCode::kUnsupported, "Permission changes are unavailable");
  return ToolResult(table_.set_site_permission(table_.self, id.value,
                                               std::to_underlying(setting)),
                    "The browser could not change this permission.");
}
Result<void> BrowserPort::ResetSitePermissions(std::string_view origin) const {
  if (!CanResetSitePermissions())
    return Fail(ErrorCode::kUnsupported,
                "Resetting permissions is unavailable");
  return ToolResult(table_.reset_site_permissions(table_.self, Borrow(origin)),
                    "The browser could not reset this site's permissions.");
}
void BrowserPort::RespondToPermissionPrompt(std::uint64_t request,
                                            PermissionDecision decision) const {
  if (CanRespondToPermissionPrompt())
    table_.permission_prompt_response(table_.self, request,
                                      std::to_underlying(decision));
}
Result<void> Shell::ShowTaskManager() {
  auto& tools = store_.EditBrowserTools();
  const bool opening = !tools.tasks_open;
  tools.OpenTasks();
  tools.can_end_tasks = browser_.CanEndTasks();
  tools.can_activate_tasks = browser_.CanActivateTasks();
  ScheduleRender();
  if (opening)
    browser_.TaskManagerVisibilityChanged(true);
  return {};
}
Result<void> Shell::UpsertTask(BrowserTask value) {
  auto result = store_.EditBrowserTools().UpsertTask(std::move(value));
  if (result)
    ScheduleRender();
  return result;
}
Result<void> Shell::RemoveTask(TaskId id) {
  auto result = store_.EditBrowserTools().RemoveTask(id);
  if (result)
    ScheduleRender();
  return result;
}
void Shell::OnTaskManagerDismissed() {
  if (!store_.browser_tools().tasks_open)
    return;
  store_.EditBrowserTools().CloseTasks();
  browser_.TaskManagerVisibilityChanged(false);
  ScheduleRender();
}
void Shell::OnTaskSearchChanged(std::string_view text) {
  store_.EditBrowserTools().SetTaskSearch(std::string(text));
  ScheduleRender();
}
void Shell::OnTaskFilterChanged(TaskFilter filter) {
  store_.EditBrowserTools().SetTaskFilter(filter);
  ScheduleRender();
}
void Shell::OnTaskSortChanged(TaskColumn column) {
  store_.EditBrowserTools().SortTasks(column);
  ScheduleRender();
}
void Shell::OnTasksSelected(std::span<const TaskId> ids) {
  store_.EditBrowserTools().SelectTasks(ids);
  ScheduleRender();
}
void Shell::OnEndTasks() {
  if (!store_.browser_tools().tasks_open ||
      !store_.browser_tools().CanEndSelection())
    return;
  // The host may synchronously remove rows while handling the command.
  const auto selected = store_.browser_tools().selected_tasks;
  const auto result = browser_.EndTasks(selected);
  auto& tools = store_.EditBrowserTools();
  tools.task_error = result ? std::string{} : result.error().message;
  ++tools.task_revision;
  ScheduleRender();
}
void Shell::OnTaskActivated(TaskId id) {
  if (store_.browser_tools().tasks_open && store_.browser_tools().FindTask(id))
    browser_.ActivateTask(id);
}
Result<void> Shell::ShowSitePermissions(std::string origin) {
  const bool opening = !store_.browser_tools().permissions_open ||
                       store_.browser_tools().permission_origin != origin;
  if (opening && store_.browser_tools().permissions_open)
    browser_.SitePermissionsVisibilityChanged(
        store_.browser_tools().permission_origin, false);
  auto& tools = store_.EditBrowserTools();
  tools.OpenPermissions(std::move(origin));
  tools.can_edit_permissions = browser_.CanEditSitePermissions();
  tools.can_reset_permissions = browser_.CanResetSitePermissions();
  ScheduleRender();
  if (opening)
    browser_.SitePermissionsVisibilityChanged(tools.permission_origin, true);
  return {};
}
Result<void> Shell::UpsertSitePermission(SitePermission value) {
  if (store_.browser_tools().permissions_open &&
      store_.browser_tools().permission_origin.empty())
    store_.EditBrowserTools().permission_origin = value.origin;
  auto result = store_.EditBrowserTools().UpsertPermission(std::move(value));
  if (result)
    ScheduleRender();
  return result;
}
Result<void> Shell::RemoveSitePermission(PermissionId id) {
  auto result = store_.EditBrowserTools().RemovePermission(id);
  if (result)
    ScheduleRender();
  return result;
}
void Shell::OnSitePermissionsDismissed() {
  if (!store_.browser_tools().permissions_open)
    return;
  const auto origin = store_.browser_tools().permission_origin;
  store_.EditBrowserTools().ClosePermissions();
  browser_.SitePermissionsVisibilityChanged(origin, false);
  ScheduleRender();
}
void Shell::OnSitePermissionsRequested() {
  const auto shown = ShowSitePermissions({});
  if (shown && store_.ui().settings.open)
    OnSettingsDismissed();
}
void Shell::OnPermissionSearchChanged(std::string_view text) {
  store_.EditBrowserTools().SetPermissionSearch(std::string(text));
  ScheduleRender();
}
void Shell::OnSitePermissionChanged(PermissionId id,
                                    PermissionSetting setting) {
  const auto& current = store_.browser_tools();
  const auto* item = current.FindPermission(id);
  if (!current.permissions_open || !current.can_edit_permissions || !item ||
      item->managed || !PermissionSettingAllowed(*item, setting))
    return;
  const auto result = browser_.SetSitePermission(id, setting);
  auto& tools = store_.EditBrowserTools();
  tools.permission_error = result ? std::string{} : result.error().message;
  ++tools.permission_revision;
  ScheduleRender();
}
void Shell::OnSitePermissionsReset() {
  const auto& current = store_.browser_tools();
  if (!current.permissions_open || !current.can_reset_permissions ||
      current.permission_origin.empty())
    return;
  const auto origin = current.permission_origin;
  const auto result = browser_.ResetSitePermissions(origin);
  auto& tools = store_.EditBrowserTools();
  tools.permission_error = result ? std::string{} : result.error().message;
  ++tools.permission_revision;
  ScheduleRender();
}
Result<void> Shell::ShowPermissionPrompt(PermissionPrompt value) {
  if (!browser_.CanRespondToPermissionPrompt())
    return Fail(ErrorCode::kUnsupported,
                "The host cannot receive permission decisions");
  // Validate before dismissing the existing request.
  BrowserToolsState validation;
  auto checked = validation.SetPrompt(std::move(value));
  if (!checked)
    return checked;
  const auto old = store_.browser_tools().prompt;
  if (old && old->id != validation.prompt->id)
    OnPermissionPromptDecision(old->id, PermissionDecision::kDismiss);
  auto result =
      store_.EditBrowserTools().SetPrompt(std::move(*validation.prompt));
  CloseAddress();
  while (!menu_anchors_.empty())
    DismissMenu(menu_anchors_.begin()->first);
  ScheduleRender();
  return result;
}
void Shell::DismissPermissionPrompt(std::uint64_t request) {
  store_.EditBrowserTools().DismissPrompt(request);
  ScheduleRender();
}
void Shell::OnPermissionPromptDecision(std::uint64_t request,
                                       PermissionDecision decision) {
  const auto& prompt = store_.browser_tools().prompt;
  if (!prompt || prompt->id != request ||
      decision > PermissionDecision::kDismiss ||
      (decision == PermissionDecision::kAllowOnce && !prompt->allow_once) ||
      (decision == PermissionDecision::kAllowAlways && !prompt->allow_always))
    return;
  store_.EditBrowserTools().DismissPrompt(request);
  ScheduleRender();
  browser_.RespondToPermissionPrompt(request, decision);
}
}  // namespace kr
