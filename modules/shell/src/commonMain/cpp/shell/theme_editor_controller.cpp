#include "krypton/ui/shell/shell.hpp"

namespace kr {
Result<void> Shell::SetWorkspaceTheme(SpaceId space,
                                      const WorkspaceTheme& theme) {
  if (!space)
    space = store_.active_space();
  auto result = store_.SetWorkspaceTheme(space, theme);
  if (result)
    ScheduleRender();
  return result;
}

Result<void> Shell::ShowThemeEditor(SpaceId space) {
  DismissBoostEditor();
  if (OnboardingActive())
    return Fail(ErrorCode::kInvalidArgument, "Onboarding is showing");
  if (!browser_.CanEditTheme())
    return Fail(ErrorCode::kUnsupported, "host does not provide theme editing");
  if (!space)
    space = store_.active_space();
  if (space && !store_.FindSpace(space))
    return Fail(ErrorCode::kNotFound, "unknown theme workspace");
  OnThemeEditorDismissed();
  OnWorkspaceEditorDismissed();
  OnTabPreviewDismissed(store_.ui().tab_preview.request_id);
  CloseAddress();
  store_.OpenThemeEditor(space);
  ScheduleRender();
  return {};
}

void Shell::OnThemeEdited(const ThemeEdit& edit) {
  if (store_.EditTheme(edit)) {
    const auto& state = store_.ui().theme_editor;
    browser_.SetWorkspaceTheme(state.space, state.value, false);
  }
  ScheduleRender();
}

void Shell::OnThemeEditorDismissed() {
  const auto& state = store_.ui().theme_editor;
  if (!state.open)
    return;
  const bool edited = state.edited;
  const auto space = state.space;
  const auto value = state.value;
  // Close before the host can re-enter with its saved state or another editor.
  store_.CloseThemeEditor();
  ScheduleRender();
  if (edited)
    browser_.SetWorkspaceTheme(space, value, true);
}
}  // namespace kr
