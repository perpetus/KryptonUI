#include <algorithm>
#include <string>
#include <vector>

#include "krypton/fixtures/mock_browser.hpp"
#include "krypton/testing/test.hpp"

namespace kr::fixtures {
namespace {
std::string Copy(KrStr text) {
  return text.data ? std::string(text.data, text.size) : std::string{};
}
struct ToolsRecorder {
  struct Task {
    KrTaskId id;
    bool can_end;
    std::string title;
  };
  struct Permission {
    KrPermissionId id;
    std::uint32_t setting;
    bool managed;
  };
  std::vector<Task> tasks;
  std::vector<Permission> permissions;
  std::size_t tabs = 0;
  int task_windows = 0, permission_windows = 0;
  std::uint64_t prompt = 0, canceled = 0;
};
KrShellUI Ui(ToolsRecorder& output) {
  KrShellUI ui{.struct_size = sizeof(KrShellUI), .self = &output};
  ui.set_theme = [](void*, const KrTheme*) {};
  ui.set_layout_prefs = [](void*, const KrLayoutPrefs*) {};
  ui.put_image = [](void*, const KrImage*) {};
  ui.drop_image = [](void*, KrImageId) {};
  ui.set_spaces = [](void*, const KrSpace*, std::size_t, std::size_t,
                     KrSpaceId) {};
  ui.tab_inserted = [](void* self, std::size_t, const KrTab*) {
    ++static_cast<ToolsRecorder*>(self)->tabs;
  };
  ui.tab_updated = [](void*, const KrTab*) {};
  ui.tab_removed = [](void* self, KrTabId) {
    --static_cast<ToolsRecorder*>(self)->tabs;
  };
  ui.set_active_tab = [](void*, KrTabId) {};
  ui.set_nav_state = [](void*, const KrNavState*) {};
  ui.set_actions = [](void*, const KrAction*, std::size_t, std::size_t) {};
  ui.focus_address = [](void*, bool) {};
  ui.show_task_manager = [](void* self) {
    ++static_cast<ToolsRecorder*>(self)->task_windows;
    return KR_OK;
  };
  ui.show_site_permissions = [](void* self, KrStr) {
    ++static_cast<ToolsRecorder*>(self)->permission_windows;
    return KR_OK;
  };
  ui.upsert_task = [](void* self, const KrBrowserTask* value) {
    auto& rows = static_cast<ToolsRecorder*>(self)->tasks;
    const auto found =
        std::ranges::find(rows, value->id, &ToolsRecorder::Task::id);
    const ToolsRecorder::Task row{value->id, value->can_end,
                                  Copy(value->title)};
    if (found == rows.end())
      rows.push_back(row);
    else
      *found = row;
    return KR_OK;
  };
  ui.remove_task = [](void* self, KrTaskId id) {
    std::erase_if(static_cast<ToolsRecorder*>(self)->tasks,
                  [&](const auto& row) { return row.id == id; });
    return KR_OK;
  };
  ui.upsert_site_permission = [](void* self, const KrSitePermission* value) {
    auto& rows = static_cast<ToolsRecorder*>(self)->permissions;
    const auto found =
        std::ranges::find(rows, value->id, &ToolsRecorder::Permission::id);
    const ToolsRecorder::Permission row{value->id, value->setting,
                                        value->managed};
    if (found == rows.end())
      rows.push_back(row);
    else
      *found = row;
    return KR_OK;
  };
  ui.show_permission_prompt = [](void* self, const KrPermissionPrompt* value) {
    static_cast<ToolsRecorder*>(self)->prompt = value->request_id;
    return KR_OK;
  };
  ui.dismiss_permission_prompt = [](void* self, std::uint64_t id) {
    static_cast<ToolsRecorder*>(self)->canceled = id;
  };
  return ui;
}
struct Host {
  ToolsRecorder output;
  MockBrowser browser{{}, {}};
  Host() { browser.Attach(Ui(output)); }
  ~Host() { browser.Detach(); }
};
KR_TEST(FixtureTaskManagerUsesNativeSurfaceAndEndsOnlyMockTabTasks) {
  Host host;
  const auto count = host.output.tabs;
  host.browser.ShowTaskManager();
  KR_EXPECT_EQ(host.output.task_windows, 1);
  KR_EXPECT_EQ(host.output.tabs, count);
  const auto& port = host.browser.table();
  port.task_manager_visibility_changed(port.self, true);
  KR_EXPECT(host.browser.task_manager_open());
  KR_EXPECT_EQ(host.output.tasks.size(), count + 2);
  const KrTaskId browser = 1;
  KR_EXPECT_EQ(port.end_tasks(port.self, &browser, 1), KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT_EQ(host.output.tabs, count);
  const auto tab = std::ranges::find_if(
      host.output.tasks, [](const auto& task) { return task.can_end; });
  KR_ASSERT(tab != host.output.tasks.end());
  const auto id = tab->id;
  const auto title = tab->title.substr(5);
  port.activate_task(port.self, id);
  KR_EXPECT_EQ(host.browser.active_page().title, title);
  KR_EXPECT_EQ(port.end_tasks(port.self, &id, 1), KR_OK);
  KR_EXPECT_EQ(host.output.tabs, count - 1);
  KR_EXPECT(
      std::ranges::find(host.output.tasks, id, &ToolsRecorder::Task::id) ==
      host.output.tasks.end());
  port.task_manager_visibility_changed(port.self, false);
  KR_EXPECT(!host.browser.task_manager_open());
}
KR_TEST(FixtureSitePermissionsConfirmChangesAndPreserveManagedPolicy) {
  Host host;
  const auto count = host.output.tabs;
  host.browser.ShowSitePermissions();
  KR_EXPECT_EQ(host.output.permission_windows, 1);
  KR_EXPECT_EQ(host.output.tabs, count);
  const auto& port = host.browser.table();
  port.site_permissions_visibility_changed(port.self, {}, true);
  KR_EXPECT_EQ(host.output.permissions.size(), 7u);
  KR_EXPECT_EQ(port.set_site_permission(port.self, 1, KR_PERMISSION_ALLOW),
               KR_OK);
  auto permission =
      std::ranges::find(host.output.permissions, KrPermissionId{1},
                        &ToolsRecorder::Permission::id);
  KR_EXPECT_EQ(permission->setting, std::uint32_t{KR_PERMISSION_ALLOW});
  KR_EXPECT_EQ(port.set_site_permission(port.self, 6, KR_PERMISSION_ALLOW),
               KR_ERR_INVALID_ARGUMENT);
  constexpr std::string_view origin = "https://www.theverge.com";
  KR_EXPECT_EQ(
      port.reset_site_permissions(port.self, {origin.data(), origin.size()}),
      KR_OK);
  permission = std::ranges::find(host.output.permissions, KrPermissionId{1},
                                 &ToolsRecorder::Permission::id);
  KR_EXPECT_EQ(permission->setting, std::uint32_t{KR_PERMISSION_DEFAULT});
  permission = std::ranges::find(host.output.permissions, KrPermissionId{6},
                                 &ToolsRecorder::Permission::id);
  KR_EXPECT(permission->managed);
  KR_EXPECT_EQ(permission->setting, std::uint32_t{KR_PERMISSION_BLOCK});
}
KR_TEST(FixturePermissionPromptAppliesDecisionsAndCancelsWhenTheTabChanges) {
  Host host;
  const auto& port = host.browser.table();
  host.browser.ShowPermissionPrompt();
  const auto request = host.output.prompt;
  KR_EXPECT(host.browser.permission_prompt_open());
  port.permission_prompt_response(port.self, request, KR_PERMISSION_ALLOW_ONCE);
  KR_EXPECT(!host.browser.permission_prompt_open());
  const auto camera =
      std::ranges::find(host.output.permissions, KrPermissionId{1},
                        &ToolsRecorder::Permission::id);
  KR_ASSERT(camera != host.output.permissions.end());
  KR_EXPECT_EQ(camera->setting, std::uint32_t{KR_PERMISSION_SESSION});
  host.browser.ShowPermissionPrompt();
  const auto pending = host.output.prompt;
  host.browser.ExecuteShortcut(PreviewShortcut::kNewTab);
  KR_EXPECT_EQ(host.output.canceled, pending);
  KR_EXPECT(!host.browser.permission_prompt_open());
}
}  // namespace
}  // namespace kr::fixtures
