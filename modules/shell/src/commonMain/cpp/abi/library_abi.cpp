#include "library_abi.hpp"

#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "krypton/ui/core/log.hpp"
#include "krypton/ui/shell/shell.hpp"

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

void FillLibraryUi(KrShellUI& ui) noexcept {
  ui.show_routing_editor = [](void* self, const KrRoutingRule* value) noexcept {
    return Call("show_routing_editor", [&]() -> Result<void> {
      if (!value || !KR_HAS_FIELD(value, KrRoutingRule, new_tabs_only) ||
          !Valid(value->pattern) || value->pattern.size > 2048 ||
          value->match > 3)
        return Fail(ErrorCode::kInvalidArgument, "Invalid routing draft");
      return From(self).ShowRoutingEditor(
          {value->id, value->revision, 0, Copy(value->pattern),
           SpaceId{value->workspace}, static_cast<RoutingMatch>(value->match),
           value->enabled, value->new_tabs_only});
    });
  };
  ui.dismiss_library = [](void* self) noexcept {
    return Call("dismiss_library", [&]() -> Result<void> {
      From(self).OnLibraryDismissed();
      return {};
    });
  };
  ui.upsert_download = [](void* self, const KrDownload* item) noexcept {
    return Call("upsert_download", [&]() -> Result<void> {
      if (!item || !KR_HAS_FIELD(item, KrDownload, icon) ||
          !Valid(item->file_name) || !Valid(item->status))
        return Fail(ErrorCode::kInvalidArgument, "Incomplete download");
      return From(self).UpsertDownload(
          {DownloadId{item->id}, static_cast<DownloadState>(item->state),
           Flags<DownloadFlag>::FromBits(item->flags), item->progress,
           Copy(item->file_name), Copy(item->status), ImageId{item->icon}});
    });
  };
  ui.remove_download = [](void* self, KrDownloadId id) noexcept {
    return Call("remove_download",
                [&] { return From(self).RemoveDownload(DownloadId{id}); });
  };
  ui.upsert_media_session = [](void* self,
                               const KrMediaSession* item) noexcept {
    return Call("upsert_media_session", [&]() -> Result<void> {
      if (!item || !KR_HAS_FIELD(item, KrMediaSession, playback_rate) ||
          !Valid(item->title) || !Valid(item->artist))
        return Fail(ErrorCode::kInvalidArgument, "Incomplete media session");
      return From(self).UpsertMediaSession(
          {MediaId{item->id}, TabId{item->tab},
           Flags<MediaFlag>::FromBits(item->flags), Copy(item->title),
           Copy(item->artist), ImageId{item->icon}, item->position,
           item->duration, item->playback_rate});
    });
  };
  ui.remove_media_session = [](void* self, KrMediaId id) noexcept {
    return Call("remove_media_session",
                [&] { return From(self).RemoveMediaSession(MediaId{id}); });
  };
  ui.set_library_section = [](void* self,
                              const KrLibrarySectionState* state) noexcept {
    return Call("set_library_section", [&]() -> Result<void> {
      if (!state || !KR_HAS_FIELD(state, KrLibrarySectionState, flags))
        return Fail(ErrorCode::kInvalidArgument, "Incomplete library section");
      return From(self).SetLibrarySection(
          static_cast<LibrarySection>(state->section),
          Flags<LibrarySectionFlag>::FromBits(state->flags));
    });
  };
  ui.upsert_library_item = [](void* self, const KrLibraryItem* item,
                              KrLibraryItemId before) noexcept {
    return Call("upsert_library_item", [&]() -> Result<void> {
      if (!item || !KR_HAS_FIELD(item, KrLibraryItem, image) ||
          !Valid(item->title) || !Valid(item->subtitle) || !Valid(item->group))
        return Fail(ErrorCode::kInvalidArgument, "Incomplete library item");
      return From(self).UpsertLibraryItem(
          {LibraryItemId{item->id}, static_cast<LibrarySection>(item->section),
           Flags<LibraryItemFlag>::FromBits(item->flags), Copy(item->title),
           Copy(item->subtitle), Copy(item->group), ImageId{item->image}},
          LibraryItemId{before});
    });
  };
  ui.remove_library_item = [](void* self, KrLibraryItemId id) noexcept {
    return Call("remove_library_item", [&] {
      return From(self).RemoveLibraryItem(LibraryItemId{id});
    });
  };
  ui.show_library = [](void* self, std::uint32_t section) noexcept {
    return Call("show_library", [&] {
      return From(self).ShowLibrary(static_cast<LibrarySection>(section));
    });
  };
}
}  // namespace kr::abi
