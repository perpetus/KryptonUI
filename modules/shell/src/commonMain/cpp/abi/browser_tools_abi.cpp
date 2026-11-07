#include "browser_tools_abi.hpp"

#include <exception>
#include <utility>

#include "krypton/ui/core/log.hpp"
#include "krypton/ui/shell/shell.hpp"
#include "payload_array.hpp"

namespace kr::abi {
namespace {
template <typename Function>
KrResult Call(std::string_view name, Function&& function) noexcept {
  try {
    const auto result = function();
    if (result)
      return KR_OK;
    Log(LogLevel::kWarning, "{}: {}", name, result.error().message);
    return result.error().code == ErrorCode::kUnsupported
               ? KR_ERR_UNSUPPORTED
               : KR_ERR_INVALID_ARGUMENT;
  } catch (const std::exception& error) {
    Log(LogLevel::kError, "{}: {}", name, error.what());
  } catch (...) {
    Log(LogLevel::kError, "{} failed", name);
  }
  return KR_ERR_PLATFORM;
}
Shell& From(void* self) {
  return *static_cast<Shell*>(self);
}
bool Valid(KrStr value) noexcept {
  return value.data || value.size == 0;
}
std::string Copy(KrStr value) {
  return value.data ? std::string(value.data, value.size) : std::string{};
}
}  // namespace
void FillBrowserToolsUi(KrShellUI& ui) noexcept {
  ui.show_task_manager = [](void* self) noexcept {
    return Call("show_task_manager",
                [&] { return From(self).ShowTaskManager(); });
  };
  ui.upsert_task = [](void* self, const KrBrowserTask* task) noexcept {
    return Call("upsert_task", [&]() -> Result<void> {
      if (!task || !KR_HAS_FIELD(task, KrBrowserTask, can_end) ||
          !Valid(task->title) || !Valid(task->profile))
        return Fail(ErrorCode::kInvalidArgument, "Incomplete browser task");
      return From(self).UpsertTask(
          {TaskId{task->id}, task->process_id,
           static_cast<TaskCategory>(task->category), Copy(task->title),
           Copy(task->profile), ImageId{task->icon}, task->memory_bytes,
           task->network_bytes_per_second, task->cpu_percent, task->can_end});
    });
  };
  ui.remove_task = [](void* self, KrTaskId id) noexcept {
    return Call("remove_task",
                [&] { return From(self).RemoveTask(TaskId{id}); });
  };
  ui.show_site_permissions = [](void* self, KrStr origin) noexcept {
    return Call("show_site_permissions", [&]() -> Result<void> {
      if (!Valid(origin))
        return Fail(ErrorCode::kInvalidArgument, "Invalid origin");
      return From(self).ShowSitePermissions(Copy(origin));
    });
  };
  ui.upsert_site_permission = [](void* self,
                                 const KrSitePermission* item) noexcept {
    return Call("upsert_site_permission", [&]() -> Result<void> {
      if (!item || !KR_HAS_FIELD(item, KrSitePermission, managed) ||
          !Valid(item->origin) || !Valid(item->label) ||
          !Valid(item->description))
        return Fail(ErrorCode::kInvalidArgument, "Incomplete site permission");
      return From(self).UpsertSitePermission(
          {PermissionId{item->id}, Copy(item->origin), Copy(item->label),
           Copy(item->description), static_cast<PermissionKind>(item->kind),
           static_cast<PermissionSetting>(item->setting),
           item->allowed_settings, item->managed});
    });
  };
  ui.remove_site_permission = [](void* self, KrPermissionId id) noexcept {
    return Call("remove_site_permission", [&] {
      return From(self).RemoveSitePermission(PermissionId{id});
    });
  };
  ui.show_permission_prompt = [](void* self,
                                 const KrPermissionPrompt* prompt) noexcept {
    return Call("show_permission_prompt", [&]() -> Result<void> {
      if (!prompt || !KR_HAS_FIELD(prompt, KrPermissionPrompt, allow_always) ||
          !Valid(prompt->origin) || !Valid(prompt->embedding_origin) ||
          !Valid(prompt->message) || prompt->request_count == 0 ||
          prompt->request_count > 16)
        return Fail(ErrorCode::kInvalidArgument,
                    "Incomplete permission prompt");
      const auto items = PayloadArray<KrPermissionRequest>::Read(
          prompt->requests, prompt->request_count, prompt->request_size,
          offsetof(KrPermissionRequest, label) +
              sizeof(KrPermissionRequest::label));
      if (!items)
        return std::unexpected(items.error());
      PermissionPrompt value{prompt->request_id,
                             Copy(prompt->origin),
                             Copy(prompt->embedding_origin),
                             Copy(prompt->message),
                             {},
                             {prompt->anchor.x, prompt->anchor.y,
                              prompt->anchor.width, prompt->anchor.height},
                             prompt->allow_once,
                             prompt->allow_always};
      value.requests.reserve(items->size());
      for (std::size_t i = 0; i < items->size(); ++i) {
        const auto item = (*items)[i];
        if (!Valid(item.label))
          return Fail(ErrorCode::kInvalidArgument, "Invalid permission label");
        value.requests.push_back(
            {static_cast<PermissionKind>(item.kind), Copy(item.label)});
      }
      return From(self).ShowPermissionPrompt(std::move(value));
    });
  };
  ui.dismiss_permission_prompt = [](void* self,
                                    std::uint64_t request) noexcept {
    (void)Call("dismiss_permission_prompt", [&]() -> Result<void> {
      From(self).DismissPermissionPrompt(request);
      return {};
    });
  };
}
}  // namespace kr::abi
