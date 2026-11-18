#include "folder_abi.hpp"

#include "krypton/ui/shell/shell.hpp"

namespace kr::abi {
static_assert(std::to_underlying(LiveFolderProvider::kOff) ==
              KR_LIVE_FOLDER_NONE);
static_assert(std::to_underlying(LiveFolderProvider::kRss) ==
              KR_LIVE_FOLDER_RSS);
static_assert(std::to_underlying(LiveFolderProvider::kPullRequests) ==
              KR_LIVE_FOLDER_GITHUB_PULLS);
static_assert(std::to_underlying(LiveFolderProvider::kIssues) ==
              KR_LIVE_FOLDER_GITHUB_ISSUES);
void FillFolderUi(KrShellUI& ui) noexcept {
  ui.upsert_folder = [](void* self, const KrFolder* value) noexcept {
    try {
      if (!value || !KR_HAS_FIELD(value, KrFolder, collapsed) ||
          (!value->name.data && value->name.size) ||
          (!value->glyph.data && value->glyph.size))
        return KR_ERR_INVALID_ARGUMENT;
      Folder folder{
          FolderId{value->id},
          SpaceId{value->space},
          FolderId{value->parent},
          value->name.size ? std::string(value->name.data, value->name.size)
                           : "",
          value->glyph.size ? std::string(value->glyph.data, value->glyph.size)
                            : "",
          value->order,
          value->collapsed};
      if (KR_HAS_FIELD(value, KrFolder, live_status)) {
        if (value->live_kind > KR_LIVE_FOLDER_GITHUB_ISSUES ||
            value->live_source.size > 2048 || value->live_status.size > 512 ||
            (!value->live_source.data && value->live_source.size) ||
            (!value->live_status.data && value->live_status.size))
          return KR_ERR_INVALID_ARGUMENT;
        folder.live_kind = value->live_kind;
        if (value->live_source.size)
          folder.live_source.assign(value->live_source.data,
                                    value->live_source.size);
        folder.live_interval_minutes = value->live_interval_minutes;
        folder.live_max_items = value->live_max_items;
        if (value->live_status.size)
          folder.live_status.assign(value->live_status.data,
                                    value->live_status.size);
        folder.live_supported = true;
      }
      return static_cast<Shell*>(self)->UpsertFolder(std::move(folder))
                 ? KR_OK
                 : KR_ERR_INVALID_ARGUMENT;
    } catch (...) {
      return KR_ERR_PLATFORM;
    }
  };
  ui.remove_folder = [](void* self, KrFolderId id) noexcept {
    try {
      return static_cast<Shell*>(self)->RemoveFolder(FolderId{id})
                 ? KR_OK
                 : KR_ERR_INVALID_ARGUMENT;
    } catch (...) {
      return KR_ERR_PLATFORM;
    }
  };
}
}  // namespace kr::abi
