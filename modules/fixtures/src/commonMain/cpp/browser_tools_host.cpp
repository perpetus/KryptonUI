#include <algorithm>
#include <array>
#include <format>

#include "krypton/fixtures/mock_browser.hpp"

namespace kr::fixtures {
namespace {
constexpr KrTaskId kTabTaskBase = 1000;
KrStr Text(std::string_view text) {
  return {text.data(), text.size()};
}
template <typename Function>
KrResult Boundary(Function&& function) noexcept {
  try {
    return function();
  } catch (...) {
    return KR_ERR_PLATFORM;
  }
}
}  // namespace
void MockBrowser::InstallBrowserTools() {
  constexpr auto ask = KR_PERMISSION_DEFAULT;
  constexpr auto choices =
      (1u << KR_PERMISSION_DEFAULT) | (1u << KR_PERMISSION_ALLOW) |
      (1u << KR_PERMISSION_BLOCK) | (1u << KR_PERMISSION_ASK);
  permissions_ = {
      {1, "https://www.theverge.com", "Camera",
       "Default: ask before using your camera", KR_PERMISSION_CAMERA, ask,
       choices | (1u << KR_PERMISSION_SESSION)},
      {2, "https://www.theverge.com", "Microphone",
       "Default: ask before using your microphone", KR_PERMISSION_MICROPHONE,
       ask, choices | (1u << KR_PERMISSION_SESSION)},
      {3, "https://www.theverge.com", "Location",
       "Default: ask before sharing your location", KR_PERMISSION_LOCATION,
       KR_PERMISSION_BLOCK, choices},
      {4, "https://www.theverge.com", "Notifications",
       "Default: ask before showing notifications", KR_PERMISSION_NOTIFICATIONS,
       KR_PERMISSION_ALLOW, choices},
      {5, "https://www.theverge.com", "Sound",
       "Default: allow sites to play sound", KR_PERMISSION_SOUND,
       KR_PERMISSION_ALLOW, choices},
      {6, "https://work.example.com", "Notifications",
       "Managed by your organization", KR_PERMISSION_NOTIFICATIONS,
       KR_PERMISSION_BLOCK, 0, true},
      {7, "https://work.example.com", "Clipboard",
       "Default: ask before reading the clipboard", KR_PERMISSION_CLIPBOARD,
       KR_PERMISSION_ASK, choices}};
  table_.task_manager_visibility_changed = [](void* self,
                                              bool visible) noexcept {
    (void)Boundary([&] {
      auto& mock = Self(self);
      mock.task_manager_open_ = visible;
      if (visible)
        mock.PushTasks();
      return KR_OK;
    });
  };
  table_.activate_task = [](void* self, KrTaskId task) noexcept {
    (void)Boundary([&] {
      auto& mock = Self(self);
      if (task >= kTabTaskBase && mock.Find(task - kTabTaskBase))
        mock.Activate(task - kTabTaskBase);
      if (mock.callbacks_.focus_page)
        mock.callbacks_.focus_page();
      return KR_OK;
    });
  };
  table_.end_tasks = [](void* self, const KrTaskId* tasks,
                        std::size_t count) noexcept {
    return Boundary([&] {
      auto& mock = Self(self);
      if (!tasks || !count)
        return KR_ERR_INVALID_ARGUMENT;
      for (std::size_t i = 0; i < count; ++i)
        if (tasks[i] < kTabTaskBase || !mock.Find(tasks[i] - kTabTaskBase))
          return KR_ERR_INVALID_ARGUMENT;
      for (std::size_t i = 0; i < count; ++i)
        if (mock.Find(tasks[i] - kTabTaskBase)) {
          mock.Close(tasks[i] - kTabTaskBase);
        }
      return KR_OK;
    });
  };
  table_.site_permissions_visibility_changed = [](void* self, KrStr origin,
                                                  bool visible) noexcept {
    (void)Boundary([&] {
      auto& mock = Self(self);
      mock.permission_manager_open_ = visible;
      mock.permission_scope_ =
          origin.data ? std::string(origin.data, origin.size) : std::string{};
      if (visible)
        mock.PushSitePermissions();
      return KR_OK;
    });
  };
  table_.set_site_permission = [](void* self, KrPermissionId id,
                                  std::uint32_t setting) noexcept {
    return Boundary([&] {
      auto& mock = Self(self);
      const auto found =
          std::ranges::find(mock.permissions_, id, &MockPermission::id);
      if (found == mock.permissions_.end() || found->managed ||
          setting > KR_PERMISSION_SESSION ||
          !(found->allowed_settings & (1u << setting)))
        return KR_ERR_INVALID_ARGUMENT;
      found->setting = setting;
      mock.PushSitePermissions();
      return KR_OK;
    });
  };
  table_.reset_site_permissions = [](void* self, KrStr origin) noexcept {
    return Boundary([&] {
      if (!origin.data || !origin.size)
        return KR_ERR_INVALID_ARGUMENT;
      auto& mock = Self(self);
      const std::string_view site(origin.data, origin.size);
      for (auto& item : mock.permissions_)
        if (item.origin == site && !item.managed)
          item.setting = KR_PERMISSION_DEFAULT;
      mock.PushSitePermissions();
      return KR_OK;
    });
  };
  table_.permission_prompt_response = [](void* self, std::uint64_t request,
                                         std::uint32_t decision) noexcept {
    (void)Boundary([&] {
      auto& mock = Self(self);
      if (!request || request != mock.permission_prompt_id_)
        return KR_ERR_INVALID_ARGUMENT;
      mock.permission_prompt_id_ = 0;
      if (decision != KR_PERMISSION_DISMISS) {
        const auto setting =
            decision == KR_PERMISSION_ALLOW_ONCE     ? KR_PERMISSION_SESSION
            : decision == KR_PERMISSION_ALLOW_ALWAYS ? KR_PERMISSION_ALLOW
                                                     : KR_PERMISSION_BLOCK;
        for (auto& item : mock.permissions_)
          if (item.origin == mock.prompt_origin_ &&
              (item.kind == KR_PERMISSION_CAMERA ||
               item.kind == KR_PERMISSION_MICROPHONE) &&
              !item.managed)
            item.setting = setting;
        mock.PushSitePermissions();
      }
      return KR_OK;
    });
  };
}
std::string MockBrowser::ActiveOrigin() const {
  const auto* tab = Find(active_tab_);
  if (!tab ||
      (!tab->url.starts_with("https://") && !tab->url.starts_with("http://")))
    return "https://www.theverge.com";
  const auto first = tab->url.find("://") + 3;
  return tab->url.substr(0, tab->url.find('/', first));
}
void MockBrowser::ShowTaskManager() {
  if (attached_ && KR_HAS_SLOT(&ui_, KrShellUI, show_task_manager) &&
      ui_.show_task_manager)
    (void)ui_.show_task_manager(ui_.self);
  else
    OpenTab("krypton://processes", KR_OPEN_NEW_TAB);
}
void MockBrowser::ShowSitePermissions(std::string_view origin) {
  if (attached_ && KR_HAS_SLOT(&ui_, KrShellUI, show_site_permissions) &&
      ui_.show_site_permissions)
    (void)ui_.show_site_permissions(ui_.self, Text(origin));
  else
    OpenTab("krypton://settings/site-permissions", KR_OPEN_NEW_TAB);
}
void MockBrowser::ShowPermissionPrompt() {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, show_permission_prompt) ||
      !ui_.show_permission_prompt)
    return;
  prompt_origin_ = ActiveOrigin();
  permission_prompt_id_ = ++next_permission_prompt_id_;
  const std::array requests{
      KrPermissionRequest{sizeof(KrPermissionRequest), KR_PERMISSION_CAMERA,
                          Text("Use your camera")},
      KrPermissionRequest{sizeof(KrPermissionRequest), KR_PERMISSION_MICROPHONE,
                          Text("Use your microphone")}};
  const KrPermissionPrompt prompt{
      sizeof(KrPermissionPrompt),
      permission_prompt_id_,
      Text(prompt_origin_),
      {},
      Text("Sample request from the preview host. Your choice applies only to "
           "this site."),
      requests.data(),
      requests.size(),
      sizeof(KrPermissionRequest),
      site_data_anchor_,
      true,
      true};
  (void)ui_.show_permission_prompt(ui_.self, &prompt);
}
void MockBrowser::CancelPermissionPrompt() {
  if (!permission_prompt_id_)
    return;
  const auto request = std::exchange(permission_prompt_id_, 0);
  if (attached_ && KR_HAS_SLOT(&ui_, KrShellUI, dismiss_permission_prompt) &&
      ui_.dismiss_permission_prompt)
    ui_.dismiss_permission_prompt(ui_.self, request);
}
void MockBrowser::PushTask(const MockTab& tab) const {
  if (!attached_ || !task_manager_open_ ||
      !KR_HAS_SLOT(&ui_, KrShellUI, upsert_task) || !ui_.upsert_task)
    return;
  const auto title = std::format("Tab: {}", tab.title);
  const KrBrowserTask task{sizeof(KrBrowserTask),
                           kTabTaskBase + tab.id,
                           static_cast<std::uint32_t>(40000 + tab.id),
                           KR_TASK_TAB,
                           Text(title),
                           Text("Preview — sample resource measurements"),
                           tab.id,
                           (72 + tab.id % 150) * 1024 * 1024,
                           tab.id == active_tab_ ? 24 * 1024u : 0u,
                           tab.id == active_tab_ ? 3.2 : 0.0,
                           true};
  (void)ui_.upsert_task(ui_.self, &task);
}
void MockBrowser::PushTasks() {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, upsert_task) ||
      !ui_.upsert_task)
    return;
  std::vector<KrTaskId> desired{1, 2};
  for (const auto& tab : tabs_)
    desired.push_back(kTabTaskBase + tab.id);
  if (KR_HAS_SLOT(&ui_, KrShellUI, remove_task) && ui_.remove_task)
    for (const auto id : published_tasks_)
      if (std::ranges::find(desired, id) == desired.end())
        (void)ui_.remove_task(ui_.self, id);
  published_tasks_ = std::move(desired);
  const std::array system{
      KrBrowserTask{sizeof(KrBrowserTask), 1, 32000, KR_TASK_BROWSER,
                    Text("Browser"),
                    Text("Preview — sample resource measurements"), 0,
                    148 * 1024 * 1024, 0, 0.8, false},
      KrBrowserTask{sizeof(KrBrowserTask), 2, 32001, KR_TASK_BROWSER,
                    Text("GPU process"),
                    Text("Preview — sample resource measurements"), 0,
                    96 * 1024 * 1024, 0, 1.4, false}};
  for (const auto& task : system)
    (void)ui_.upsert_task(ui_.self, &task);
  for (const auto& tab : tabs_)
    PushTask(tab);
}
void MockBrowser::PushSitePermissions() const {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, upsert_site_permission) ||
      !ui_.upsert_site_permission)
    return;
  for (const auto& item : permissions_) {
    if (permission_manager_open_ && !permission_scope_.empty() &&
        item.origin != permission_scope_)
      continue;
    const KrSitePermission value{sizeof(KrSitePermission),
                                 item.id,
                                 Text(item.origin),
                                 Text(item.label),
                                 Text(item.description),
                                 item.kind,
                                 item.setting,
                                 item.allowed_settings,
                                 item.managed};
    (void)ui_.upsert_site_permission(ui_.self, &value);
  }
}
}  // namespace kr::fixtures
