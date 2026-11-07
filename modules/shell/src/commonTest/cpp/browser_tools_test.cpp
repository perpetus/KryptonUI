#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {
KrStr Text(std::string_view value) {
  return {value.data(), value.size()};
}
struct ToolsHost {
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};
  int task_opens = 0, task_closes = 0, permission_opens = 0,
      permission_closes = 0;
  int permission_edits = 0, permission_resets = 0;
  bool confirm_end = false;
  KrResult edit_result = KR_OK;
  std::vector<KrTaskId> ended;
  std::vector<std::pair<std::uint64_t, std::uint32_t>> decisions;
  std::string origin;
  KrPermissionId edited_permission = 0;
  std::uint32_t edited_setting = 0;
  explicit ToolsHost(bool capabilities = true) {
    headless::Reset();
    KrBrowser browser{
        .struct_size =
            capabilities ? sizeof(KrBrowser)
                         : offsetof(KrBrowser, task_manager_visibility_changed),
        .self = this};
    browser.task_manager_visibility_changed = [](void* self, bool visible) {
      auto& host = *static_cast<ToolsHost*>(self);
      visible ? ++host.task_opens : ++host.task_closes;
    };
    browser.end_tasks = [](void* self, const KrTaskId* ids, std::size_t count) {
      auto& host = *static_cast<ToolsHost*>(self);
      host.ended.assign(ids, ids + count);
      if (host.confirm_end)
        for (auto id : host.ended)
          (void)host.ui.remove_task(host.ui.self, id);
      return host.edit_result;
    };
    browser.site_permissions_visibility_changed = [](void* self, KrStr origin,
                                                     bool visible) {
      auto& host = *static_cast<ToolsHost*>(self);
      host.origin =
          origin.data ? std::string(origin.data, origin.size) : std::string{};
      visible ? ++host.permission_opens : ++host.permission_closes;
    };
    browser.set_site_permission = [](void* self, KrPermissionId id,
                                     std::uint32_t setting) {
      auto& host = *static_cast<ToolsHost*>(self);
      ++host.permission_edits;
      host.edited_permission = id;
      host.edited_setting = setting;
      return host.edit_result;
    };
    browser.reset_site_permissions = [](void* self, KrStr origin) {
      auto& host = *static_cast<ToolsHost*>(self);
      ++host.permission_resets;
      host.origin.assign(origin.data, origin.size);
      return host.edit_result;
    };
    browser.permission_prompt_response = [](void* self, std::uint64_t request,
                                            std::uint32_t decision) {
      static_cast<ToolsHost*>(self)->decisions.emplace_back(request, decision);
    };
    const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                                &browser};
    KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
    headless::RunPendingTasks();
  }
  ~ToolsHost() { Destroy(); }
  void Destroy() {
    if (ui.self) {
      ui.destroy(std::exchange(ui.self, nullptr));
      headless::RunPendingTasks();
    }
  }
  void Task(KrTaskId id,
            std::string_view title,
            std::uint64_t memory,
            bool killable = true,
            std::uint32_t category = KR_TASK_TAB) {
    const KrBrowserTask task{sizeof(task),
                             id,
                             static_cast<std::uint32_t>(id + 100),
                             category,
                             Text(title),
                             Text("Profile"),
                             0,
                             memory,
                             0,
                             1.5,
                             killable};
    KR_EXPECT_EQ(ui.upsert_task(ui.self, &task), KR_OK);
  }
  void Permission(KrPermissionId id,
                  bool managed = false,
                  std::uint32_t setting = KR_PERMISSION_ASK) {
    const KrSitePermission permission{sizeof(permission),
                                      id,
                                      Text("https://example.com"),
                                      Text("Camera"),
                                      {},
                                      KR_PERMISSION_CAMERA,
                                      setting,
                                      managed ? 0u : 0x0fu,
                                      managed};
    KR_EXPECT_EQ(ui.upsert_site_permission(ui.self, &permission), KR_OK);
  }
  KrResult Prompt(std::uint64_t id, bool once = true, bool always = true) {
    const KrPermissionRequest item{sizeof(item), KR_PERMISSION_CAMERA,
                                   Text("Use your camera")};
    const KrPermissionPrompt prompt{sizeof(prompt),
                                    id,
                                    Text("https://example.com"),
                                    {},
                                    {},
                                    &item,
                                    1,
                                    sizeof(item),
                                    {20, 30, 24, 24},
                                    once,
                                    always};
    return ui.show_permission_prompt(ui.self, &prompt);
  }
};

KR_TEST(BrowserToolsTasksCoalesceSortFilterAndCopyHostStrings) {
  ToolsHost host;
  const auto before = headless::RenderCount();
  KR_EXPECT_EQ(host.ui.show_task_manager(host.ui.self), KR_OK);
  std::string title = "Tab Alpha";
  host.Task(1, title, 1024);
  host.Task(2, "Tab Beta", 4096);
  host.Task(3, "Browser", 8192, false, KR_TASK_BROWSER);
  title.assign("Overwritten producer storage");
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::RenderCount(), before + 1);
  KR_EXPECT(headless::LastRender().tasks_open);
  KR_ASSERT(headless::LastRender().tasks.size() == 2);
  KR_EXPECT_EQ(headless::LastRender().tasks[0].title, std::string("Tab Alpha"));
  KR_EXPECT_EQ(host.task_opens, 1);
  auto* delegate = headless::Delegate();
  delegate->OnTaskSortChanged(TaskColumn::kMemory);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::LastRender().tasks[0].id, TaskId{2});
  delegate->OnTaskFilterChanged(TaskFilter::kAll);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::LastRender().tasks.size(), 3u);
  delegate->OnTaskSearchChanged("alpha");
  headless::RunPendingTasks();
  KR_ASSERT(headless::LastRender().tasks.size() == 1);
  KR_EXPECT_EQ(headless::LastRender().tasks[0].id, TaskId{1});
  KR_EXPECT_EQ(host.ui.show_task_manager(host.ui.self), KR_OK);
  KR_EXPECT_EQ(host.task_opens, 1);
}

KR_TEST(BrowserToolsProtectedTasksCannotEndAndReentrantRemovalIsSafe) {
  ToolsHost host;
  KR_ASSERT(host.ui.show_task_manager(host.ui.self) == KR_OK);
  host.Task(1, "Tab", 1024);
  host.Task(2, "Browser", 4096, false, KR_TASK_BROWSER);
  auto* delegate = headless::Delegate();
  const std::array protected_selection{TaskId{1}, TaskId{2}};
  delegate->OnTaskFilterChanged(TaskFilter::kAll);
  delegate->OnTasksSelected(protected_selection);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().can_end_tasks);
  delegate->OnEndTasks();
  KR_EXPECT(host.ended.empty());
  const std::array selection{TaskId{1}};
  delegate->OnTasksSelected(selection);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().can_end_tasks);
  host.confirm_end = true;
  delegate->OnEndTasks();
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.ended.size(), 1u);
  KR_EXPECT_EQ(host.ended.front(), KrTaskId{1});
  KR_EXPECT(!headless::LastRender().can_end_tasks);
  delegate->OnTaskManagerDismissed();
  delegate->OnTaskManagerDismissed();
  KR_EXPECT_EQ(host.task_closes, 1);
}

KR_TEST(BrowserToolsSearchClearsTaskSelectionAndHostFailureIsVisible) {
  ToolsHost host;
  KR_ASSERT(host.ui.show_task_manager(host.ui.self) == KR_OK);
  host.Task(1, "Alpha", 1024);
  const std::array selection{TaskId{1}};
  auto* delegate = headless::Delegate();
  delegate->OnTasksSelected(selection);
  host.edit_result = KR_ERR_PLATFORM;
  delegate->OnEndTasks();
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().task_error.empty());
  delegate->OnTaskSearchChanged("other");
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().can_end_tasks);
  KR_EXPECT(headless::LastRender().tasks.empty());
}

KR_TEST(BrowserToolsPermissionsArePolicyAwareAndConfirmedByHostUpdates) {
  ToolsHost host;
  KR_ASSERT(host.ui.show_site_permissions(host.ui.self, {}) == KR_OK);
  host.Permission(1);
  host.Permission(2, true, KR_PERMISSION_BLOCK);
  headless::RunPendingTasks();
  KR_ASSERT(headless::LastRender().permissions.size() == 2);
  auto* delegate = headless::Delegate();
  delegate->OnSitePermissionChanged(PermissionId{2}, PermissionSetting::kAllow);
  delegate->OnSitePermissionChanged(PermissionId{1},
                                    PermissionSetting::kSession);
  KR_EXPECT_EQ(host.permission_edits, 0);
  delegate->OnSitePermissionChanged(PermissionId{1}, PermissionSetting::kBlock);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.permission_edits, 1);
  KR_EXPECT_EQ(host.edited_permission, KrPermissionId{1});
  KR_EXPECT_EQ(host.edited_setting, std::uint32_t{KR_PERMISSION_BLOCK});
  KR_EXPECT_EQ(headless::LastRender().permissions[0].setting,
               PermissionSetting::kAsk);
  host.Permission(1, false, KR_PERMISSION_BLOCK);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::LastRender().permissions[0].setting,
               PermissionSetting::kBlock);
  delegate->OnSitePermissionsReset();
  KR_EXPECT_EQ(host.permission_resets, 0);
  KR_ASSERT(host.ui.show_site_permissions(
                host.ui.self, Text("https://example.com")) == KR_OK);
  delegate->OnSitePermissionsReset();
  KR_EXPECT_EQ(host.permission_resets, 1);
  KR_EXPECT_EQ(host.origin, std::string("https://example.com"));
  delegate->OnSitePermissionsDismissed();
  KR_EXPECT_EQ(host.permission_closes, 2);
}

KR_TEST(BrowserToolsPermissionPromptRespondsOnceAndHonorsHostCancellation) {
  ToolsHost host;
  KR_ASSERT(host.Prompt(1, true, false) == KR_OK);
  headless::RunPendingTasks();
  KR_ASSERT(headless::LastRender().permission_prompt.has_value());
  auto* delegate = headless::Delegate();
  delegate->OnPermissionPromptDecision(1, PermissionDecision::kAllowAlways);
  KR_EXPECT(host.decisions.empty());
  delegate->OnPermissionPromptDecision(1, PermissionDecision::kAllowOnce);
  delegate->OnPermissionPromptDecision(1, PermissionDecision::kDismiss);
  KR_ASSERT(host.decisions.size() == 1);
  KR_EXPECT_EQ(host.decisions[0].second,
               std::uint32_t{KR_PERMISSION_ALLOW_ONCE});
  KR_ASSERT(host.Prompt(2) == KR_OK);
  host.ui.dismiss_permission_prompt(host.ui.self, 1);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().permission_prompt.has_value());
  host.ui.dismiss_permission_prompt(host.ui.self, 2);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().permission_prompt.has_value());
  KR_EXPECT_EQ(host.decisions.size(), 1u);
  KR_ASSERT(host.Prompt(3) == KR_OK);
  KR_ASSERT(host.Prompt(4) == KR_OK);
  KR_EXPECT_EQ(host.decisions.size(), 2u);
  KR_EXPECT_EQ(host.decisions.back().first, std::uint64_t{3});
  KR_EXPECT_EQ(host.decisions.back().second,
               std::uint32_t{KR_PERMISSION_DISMISS});
  host.Destroy();
  KR_EXPECT_EQ(host.decisions.size(), 3u);
  KR_EXPECT_EQ(host.decisions.back().first, std::uint64_t{4});
}

KR_TEST(BrowserToolsRejectMalformedMetricsAndCopyPermissionRequests) {
  ToolsHost host;
  KrBrowserTask task{.struct_size = sizeof(task),
                     .id = 1,
                     .cpu_percent = std::numeric_limits<double>::quiet_NaN()};
  KR_EXPECT_EQ(host.ui.upsert_task(host.ui.self, &task),
               KR_ERR_INVALID_ARGUMENT);
  task.cpu_percent = 1;
  task.struct_size = offsetof(KrBrowserTask, cpu_percent);
  KR_EXPECT_EQ(host.ui.upsert_task(host.ui.self, &task),
               KR_ERR_INVALID_ARGUMENT);
  std::string origin = "https://example.com", label = "Use your camera";
  const KrPermissionRequest request{sizeof(request), KR_PERMISSION_CAMERA,
                                    Text(label)};
  KrPermissionPrompt prompt{
      sizeof(prompt),  8,  Text(origin), {},  {}, &request, 1,
      sizeof(request), {}, true,         true};
  KR_ASSERT(host.ui.show_permission_prompt(host.ui.self, &prompt) == KR_OK);
  origin.clear();
  label.clear();
  headless::RunPendingTasks();
  KR_ASSERT(headless::LastRender().permission_prompt.has_value());
  KR_EXPECT_EQ(headless::LastRender().permission_prompt->origin,
               std::string("https://example.com"));
  KR_EXPECT_EQ(headless::LastRender().permission_prompt->requests[0].label,
               std::string("Use your camera"));
  prompt.request_id = 9;
  prompt.request_count = 17;
  KR_EXPECT_EQ(host.ui.show_permission_prompt(host.ui.self, &prompt),
               KR_ERR_INVALID_ARGUMENT);
}

KR_TEST(BrowserToolsMissingOptionalHostServicesRemainReadOnly) {
  ToolsHost host(false);
  KR_ASSERT(host.ui.show_task_manager(host.ui.self) == KR_OK);
  host.Task(1, "Tab", 1024);
  const std::array selection{TaskId{1}};
  headless::Delegate()->OnTasksSelected(selection);
  KR_ASSERT(host.ui.show_site_permissions(host.ui.self, {}) == KR_OK);
  host.Permission(1);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().can_end_tasks);
  KR_ASSERT(headless::LastRender().permissions.size() == 1);
  KR_EXPECT(!headless::LastRender().permissions[0].editable);
  KR_EXPECT_EQ(host.Prompt(1), KR_ERR_UNSUPPORTED);
}
}  // namespace
}  // namespace kr
