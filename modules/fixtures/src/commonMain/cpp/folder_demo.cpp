#include "krypton/fixtures/mock_browser.hpp"

namespace kr::fixtures {
void MockBrowser::SeedFolderDemo() {
  if (!folders_.empty())
    return;
  const auto create = [&](std::string_view name, KrFolderId parent) {
    const auto id = next_folder_id_;
    const KrFolderCommand command{.struct_size = sizeof(KrFolderCommand),
                                  .kind = KR_FOLDER_CREATE,
                                  .parent = parent,
                                  .space = active_space_,
                                  .name = {name.data(), name.size()}};
    return ExecuteFolderCommand(command) == KR_OK ? id : KrFolderId{};
  };
  const auto first = create("Research", 0);
  const auto second = create("Projects", 0);
  const auto child = create("Drafts", second);
  if (!first || !second || !child)
    return;
  for (auto& tab : tabs_)
    if (tab.space == active_space_ && !(tab.flags & KR_TAB_FAVORITE)) {
      tab.folder = tab.id % 2 ? first : child;
      tab.flags |= KR_TAB_PINNED;
      PushTab(tab);
    }
}
}  // namespace kr::fixtures
