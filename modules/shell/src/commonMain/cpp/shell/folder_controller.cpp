#include "krypton/ui/shell/shell.hpp"

namespace kr {
std::optional<FolderVm> Shell::OnFolderRequested(FolderId id) const {
  const auto* folder = store_.FindFolder(id);
  if (!folder)
    return std::nullopt;
  std::size_t depth = 1;
  for (const auto* parent = store_.FindFolder(folder->parent); parent;
       parent = store_.FindFolder(parent->parent))
    ++depth;
  return FolderVm{
      folder->id,    folder->parent,    folder->name,           folder->glyph,
      folder->order, folder->collapsed, depth < kMaxFolderDepth};
}
bool BrowserPort::CanFolders() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, folder_command) &&
         table_.folder_command;
}
Result<void> BrowserPort::FolderCommand(
    const kr::FolderCommand& command) const {
  if (!CanFolders())
    return Fail(ErrorCode::kUnsupported, "Folders are unavailable");
  const KrFolderCommand value{sizeof(KrFolderCommand),
                              static_cast<std::uint32_t>(command.kind),
                              command.folder.value,
                              command.parent.value,
                              command.tab.value,
                              command.space.value,
                              command.index,
                              {command.name.data(), command.name.size()},
                              {command.glyph.data(), command.glyph.size()},
                              command.collapsed};
  if (table_.folder_command(table_.self, &value) != KR_OK)
    return Fail(ErrorCode::kInvalidArgument, "Folder command was rejected");
  return {};
}
Result<void> Shell::UpsertFolder(Folder folder) {
  auto result = store_.UpsertFolder(std::move(folder));
  if (result)
    ScheduleRender();
  return result;
}
Result<void> Shell::RemoveFolder(FolderId id) {
  auto result = store_.RemoveFolder(id);
  if (result)
    ScheduleRender();
  return result;
}
Result<void> Shell::OnFolderCommand(const FolderCommand& command) {
  if (OnboardingActive())
    return Fail(ErrorCode::kUnsupported, "Onboarding is active");
  auto value = command;
  if (!value.space)
    value.space = store_.active_space();
  store_.InvalidateTabs();
  ScheduleRender();
  // Delete may close the final tab and destroy this shell synchronously.
  return browser_.FolderCommand(value);
}
}  // namespace kr
