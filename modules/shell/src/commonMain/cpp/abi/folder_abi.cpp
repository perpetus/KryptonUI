#include "folder_abi.hpp"

#include "krypton/ui/shell/shell.hpp"

namespace kr::abi {
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
