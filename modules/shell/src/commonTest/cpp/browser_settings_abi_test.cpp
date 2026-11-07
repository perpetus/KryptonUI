#include <algorithm>
#include <cstddef>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {
struct BrowserSettingsHost {
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};
  std::uint64_t revision = 0, request = 0;
  int edits = 0, actions = 0, defaults = 0, workspace_edits = 0;
  std::uint32_t startup = 0;
  bool synchronous = false;
  std::vector<std::pair<std::uint32_t, bool>> visibility;
  explicit BrowserSettingsHost(std::uint32_t size = sizeof(KrBrowser)) {
    headless::Reset();
    KrBrowser host{.struct_size = size, .self = this};
    host.settings_visibility_changed = [](void* self, std::uint32_t section,
                                          bool visible) noexcept {
      static_cast<BrowserSettingsHost*>(self)->visibility.emplace_back(section,
                                                                       visible);
    };
    host.edit_setting = [](void* self, const KrSettingsEdit* edit) noexcept {
      auto& h = *static_cast<BrowserSettingsHost*>(self);
      ++h.edits;
      h.request = edit->request_id;
      if (h.synchronous) {
        h.startup = edit->value;
        h.Publish();
        h.Complete(KR_OK);
      }
      return KR_OK;
    };
    host.invoke_settings_action = [](void* self, std::uint64_t request,
                                     std::uint32_t) noexcept {
      auto& h = *static_cast<BrowserSettingsHost*>(self);
      ++h.actions;
      h.request = request;
      return KR_OK;
    };
    host.request_default_browser = [](void* self) noexcept {
      auto& h = *static_cast<BrowserSettingsHost*>(self);
      ++h.defaults;
      const KrDefaultBrowserState state{sizeof(KrDefaultBrowserState),
                                        KR_DEFAULT_BROWSER_IS_DEFAULT};
      return h.ui.set_default_browser_state(h.ui.self, &state);
    };
    host.edit_workspace = [](void* self, std::uint32_t,
                             const KrSpace*) noexcept {
      ++static_cast<BrowserSettingsHost*>(self)->workspace_edits;
    };
    const KrCreateParams params{sizeof(KrCreateParams), KR_UI_ABI_VERSION,
                                nullptr, &host};
    KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
    KR_ASSERT(ui.show_settings(ui.self, KR_SETTINGS_GENERAL) == KR_OK);
    Publish();
    headless::RunPendingTasks();
  }
  ~BrowserSettingsHost() {
    ui.destroy(ui.self);
    headless::Reset();
  }
  void Publish(std::uint64_t managed = 0) {
    const KrSettingsSnapshot value{
        sizeof(KrSettingsSnapshot),
        ++revision,
        (UINT64_C(1) << KR_SETTINGS_STARTUP) |
            (UINT64_C(1) << KR_SETTINGS_DEFAULT_BROWSER),
        managed,
        (UINT64_C(1) << KR_SETTINGS_ACTION_IMPORT) |
            (UINT64_C(1) << KR_SETTINGS_ACTION_MAKE_DEFAULT),
        startup,
        false,
        {"Profile", 7},
        {},
        {},
        {},
        {}};
    KR_ASSERT(ui.set_settings_snapshot(ui.self, &value) == KR_OK);
  }
  void Complete(KrResult result) {
    const KrSettingsOperationResult value{sizeof(KrSettingsOperationResult),
                                          request, result};
    KR_ASSERT(ui.settings_operation_result(ui.self, &value) == KR_OK);
  }
};
}  // namespace
KR_TEST(BrowserSettingsAbiSynchronousConfirmationCannotOverwritePendingState) {
  BrowserSettingsHost h;
  h.synchronous = true;
  headless::Delegate()->OnBrowserSettingEdited(BrowserSetting::kStartup, 1);
  headless::RunPendingTasks();
  const auto& state = *headless::LastRender().settings.browser;
  KR_EXPECT_EQ(h.edits, 1);
  KR_EXPECT_EQ(state.value.startup, 1u);
  KR_EXPECT(!state.Pending(BrowserSetting::kStartup));
  KR_EXPECT(!state.error);
}
KR_TEST(BrowserSettingsAbiWaitsForAuthoritativeSnapshotAndRejectsManagedEdits) {
  BrowserSettingsHost h;
  headless::Delegate()->OnBrowserSettingEdited(BrowserSetting::kStartup, 2);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::LastRender().settings.browser->value.startup, 0u);
  KR_EXPECT(headless::LastRender().settings.browser->Pending(
      BrowserSetting::kStartup));
  h.Complete(KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().settings.browser->error);
  h.Publish(UINT64_C(1) << KR_SETTINGS_STARTUP);
  headless::Delegate()->OnBrowserSettingEdited(BrowserSetting::kStartup, 1);
  KR_EXPECT_EQ(h.edits, 1);
}
KR_TEST(BrowserSettingsAbiHandlesTruncatedHostSlotsAndInvalidPayloads) {
  BrowserSettingsHost h(
      static_cast<std::uint32_t>(offsetof(KrBrowser, edit_setting) + 1));
  KR_EXPECT(!headless::LastRender().settings.browser->editable);
  headless::Delegate()->OnBrowserSettingEdited(BrowserSetting::kStartup, 1);
  KR_EXPECT_EQ(h.edits, 0);
  KR_EXPECT(h.ui.set_settings_snapshot(h.ui.self, nullptr) ==
            KR_ERR_INVALID_ARGUMENT);
  KrSettingsSnapshot snapshot{.struct_size = sizeof(KrSettingsSnapshot)};
  snapshot.profile_name = {nullptr, 8};
  KR_EXPECT(h.ui.set_settings_snapshot(h.ui.self, &snapshot) ==
            KR_ERR_INVALID_ARGUMENT);
  snapshot.profile_name = {};
  snapshot.struct_size = offsetof(KrSettingsSnapshot, build);
  KR_EXPECT(h.ui.set_settings_snapshot(h.ui.self, &snapshot) ==
            KR_ERR_INVALID_ARGUMENT);
}
KR_TEST(BrowserSettingsAbiActionsRespectSectionAndReuseDefaultBrowserIntent) {
  BrowserSettingsHost h;
  const KrDefaultBrowserState state{sizeof(KrDefaultBrowserState),
                                    KR_DEFAULT_BROWSER_CAN_SET};
  KR_ASSERT(h.ui.set_default_browser_state(h.ui.self, &state) == KR_OK);
  headless::Delegate()->OnSettingsAction(SettingsAction::kMakeDefault);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(h.defaults, 1);
  KR_EXPECT_EQ(h.actions, 0);
  KR_EXPECT_EQ(headless::LastRender().settings.browser->action.request,
               UINT64_C(0));
  KR_ASSERT(h.ui.show_settings(h.ui.self, KR_SETTINGS_APPEARANCE) == KR_OK);
  headless::Delegate()->OnSettingsAction(SettingsAction::kImport);
  KR_EXPECT_EQ(h.actions, 0);
}
KR_TEST(SettingsOwnedWorkspaceSubmissionKeepsSettingsOpenAndProtectsDefault) {
  BrowserSettingsHost h;
  const KrSpace spaces[]{{sizeof(KrSpace), 1, {"Default", 7}, {}, 0},
                         {sizeof(KrSpace), 2, {"Work", 4}, {}, 0}};
  h.ui.set_spaces(h.ui.self, spaces, 2, sizeof(KrSpace), 1);
  KR_ASSERT(h.ui.show_settings(h.ui.self, KR_SETTINGS_WORKSPACES) == KR_OK);
  KR_EXPECT(!headless::Delegate()->OnSettingsWorkspaceSubmitted(
      WorkspaceEditKind::kRemove, SpaceId{1}, "", ""));
  KR_EXPECT(headless::Delegate()->OnSettingsWorkspaceSubmitted(
      WorkspaceEditKind::kUpsert, SpaceId{2}, "Research", "R"));
  headless::RunPendingTasks();
  KR_EXPECT_EQ(h.workspace_edits, 1);
  KR_EXPECT(headless::LastRender().settings.state.open);
  KR_EXPECT(!headless::LastRender().workspace_editor_open);
  KR_EXPECT((h.visibility.back() ==
             std::pair<std::uint32_t, bool>{KR_SETTINGS_WORKSPACES, true}));
  headless::Delegate()->OnSettingsDismissed();
  KR_EXPECT((h.visibility.back() ==
             std::pair<std::uint32_t, bool>{KR_SETTINGS_WORKSPACES, false}));
}
KR_TEST(SettingsRestoreRequestKeepsTheRetainedSection) {
  BrowserSettingsHost h;
  KR_ASSERT(h.ui.show_settings(h.ui.self, KR_SETTINGS_DOWNLOADS) == KR_OK);
  headless::Delegate()->OnSettingsDismissed();
  KR_ASSERT(h.ui.show_settings(h.ui.self, KR_SETTINGS_CURRENT) == KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().settings.state.section ==
            SettingsSection::kDownloads);
}

}  // namespace kr
