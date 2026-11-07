#include "krypton/ui/model/workspace_editor.hpp"
namespace kr {
Result<Space> PrepareWorkspaceEdit(WorkspaceEditKind kind,
                                   const Space* existing,
                                   bool can_remove,
                                   std::string_view name,
                                   std::string_view glyph,
                                   Color accent) {
  if (kind > WorkspaceEditKind::kRemove ||
      (kind == WorkspaceEditKind::kRemove && (!existing || !can_remove)))
    return Fail(ErrorCode::kInvalidArgument, "Workspace cannot be removed");
  constexpr std::string_view whitespace = " \t\r\n";
  const auto first = name.find_first_not_of(whitespace);
  if (kind == WorkspaceEditKind::kUpsert &&
      (first == std::string_view::npos || name.contains('\0') ||
       glyph.contains('\0')))
    return Fail(ErrorCode::kInvalidArgument, "Workspace name is required");
  Space value = existing ? *existing : Space{.color = accent};
  if (kind == WorkspaceEditKind::kUpsert) {
    value.name =
        name.substr(first, name.find_last_not_of(whitespace) - first + 1);
    value.glyph = glyph;
  }
  return value;
}
}  // namespace kr
