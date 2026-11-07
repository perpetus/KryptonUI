#include "krypton/ui/presentation/theme_editor.hpp"
#include "krypton/ui/state/store.hpp"

namespace kr {
Result<void> Store::SetWorkspaceTheme(SpaceId space,
                                      const WorkspaceTheme& theme) {
  if (!ValidWorkspaceTheme(theme))
    return Fail(ErrorCode::kInvalidArgument, "invalid workspace theme");
  const auto found = workspace_themes_.find(space);
  if (found != workspace_themes_.end() && found->second == theme)
    return {};
  workspace_themes_.insert_or_assign(space, theme);
  if (ui_.theme_editor.open && ui_.theme_editor.space == space &&
      ui_.theme_editor.value != theme) {
    ui_.theme_editor.value = theme;
    MarkDirty(Dirty::kThemeEditor);
  }
  MarkDirty(Dirty::kTheme);
  return {};
}

const WorkspaceTheme* Store::WorkspaceThemeFor(SpaceId space) const noexcept {
  if (onboarding_.theme_editing && onboarding_.theme_space == space)
    return &onboarding_.theme;
  if (ui_.theme_editor.open && ui_.theme_editor.space == space)
    return &ui_.theme_editor.value;
  const auto found = workspace_themes_.find(space);
  return found == workspace_themes_.end() ? nullptr : &found->second;
}

void Store::OpenThemeEditor(SpaceId space) {
  const auto* current = WorkspaceThemeFor(space);
  WorkspaceTheme value;
  if (current)
    value = *current;
  if (!current || !current->customized) {
    const auto* workspace = FindSpace(space);
    value = ThemeFromAccent(workspace ? workspace->color : theme_.accent,
                            current ? current->scheme : ThemeScheme::kSystem);
  }
  ui_.theme_editor = {.open = true, .space = space, .value = value};
  MarkDirty(Dirty::kThemeEditor);
}

void Store::SetOnboardingThemeEditing(bool editing) {
  if (onboarding_.theme_editing == editing)
    return;
  onboarding_.theme_editing = editing;
  MarkDirty(Dirty::kOnboarding | Dirty::kTheme);
}

bool Store::EditOnboardingTheme(const ThemeEdit& edit) {
  if (!onboarding_.theme_editing ||
      !EditWorkspaceTheme(onboarding_.theme, edit))
    return false;
  onboarding_.theme_edited = true;
  MarkDirty(Dirty::kOnboarding | Dirty::kTheme);
  return true;
}

void Store::SetOnboardingLanding(const Anchor& anchor) {
  if (onboarding_.open && onboarding_.SetLanding(anchor))
    MarkDirty(Dirty::kOnboarding);
}

void Store::CloseThemeEditor() {
  if (!ui_.theme_editor.open)
    return;
  ui_.theme_editor.open = false;
  MarkDirty(Dirty::kThemeEditor | Dirty::kTheme);
}

bool Store::EditTheme(const ThemeEdit& edit) {
  auto& editor = ui_.theme_editor;
  if (!editor.open)
    return false;
  if (edit.kind == ThemeEditKind::kPreviousPage ||
      edit.kind == ThemeEditKind::kNextPage) {
    constexpr std::size_t pages = kThemePresetPageCount;
    const auto page = edit.kind == ThemeEditKind::kPreviousPage
                          ? (editor.page == 0 ? 0 : editor.page - 1)
                          : std::min(pages - 1, editor.page + 1);
    if (page == editor.page)
      return false;
    editor.page = page;
    MarkDirty(Dirty::kThemeEditor);
    return false;
  }
  if (!EditWorkspaceTheme(editor.value, edit))
    return false;
  editor.edited = true;
  MarkDirty(Dirty::kThemeEditor | Dirty::kTheme);
  return true;
}
}  // namespace kr
