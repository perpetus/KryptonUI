#include <algorithm>
#include <string_view>
#include <utility>

#include "krypton/ui/shell/shell.hpp"

namespace kr {
bool Shell::OnWorkspaceSwipe(WorkspaceSwipeInput input) {
  auto& swipe = store_.EditWorkspaceSwipe();
  const auto& ui = store_.ui();
  const auto spaces = store_.spaces();
  if (!store_.layout_prefs().flags.Has(LayoutFlag::kWorkspaceSwipe) ||
      !browser_.CanSwitchWorkspaces() || spaces.size() < 2 ||
      OnboardingActive() || ui.settings.open || ui.command_bar.open ||
      ui.theme_editor.open || ui.workspace_editor.open ||
      ui.boost_editor.open || ui.split_chooser.open || !menu_anchors_.empty() ||
      store_.library().open) {
    swipe.Reset();
    return false;
  }
  const auto update = swipe.Update(input);
  if (update.direction) {
    const auto active =
        std::ranges::find(spaces, store_.active_space(), &Space::id);
    if (active != spaces.end()) {
      const auto index = static_cast<std::size_t>(active - spaces.begin());
      const auto next = *update.direction == WorkspaceSwipeDirection::kNext
                            ? (index + 1) % spaces.size()
                            : (index + spaces.size() - 1) % spaces.size();
      OnTabPreviewDismissed(ui.tab_preview.request_id);
      browser_.SwitchSpace(spaces[next].id);
    }
  }
  return update.consume;
}

Result<void> Shell::ShowTabIconEditor(TabId tab, std::string_view glyph) {
  DismissBoostEditor();
  if (OnboardingActive() || !browser_.CanSetTabIcon() || !store_.FindTab(tab))
    return Fail(ErrorCode::kInvalidArgument, "Icon editing is unavailable");
  CloseAddress();
  OnThemeEditorDismissed();
  OnTabPreviewDismissed(store_.ui().tab_preview.request_id);
  if (store_.ui().settings.open)
    OnSettingsDismissed();
  if (!menu_anchors_.empty())
    DismissMenu(menu_anchors_.begin()->first);
  store_.OpenWorkspaceEditor(Space{.name = "Icon", .glyph = std::string(glyph)},
                             true, tab);
  ScheduleRender();
  return {};
}
Result<void> Shell::ShowWorkspaceEditor(SpaceId space) {
  DismissBoostEditor();
  if (OnboardingActive())
    return Fail(ErrorCode::kInvalidArgument, "Onboarding is showing");
  if (!browser_.CanEditWorkspaces())
    return Fail(ErrorCode::kUnsupported,
                "Host does not provide workspace editing");
  const auto* found = store_.FindSpace(space);
  if (space && !found)
    return Fail(ErrorCode::kNotFound, "Unknown workspace");
  const bool can_remove =
      found && !store_.spaces().empty() && store_.spaces().front().id != space;
  Space value = found ? *found : Space{.color = store_.theme().accent};
  CloseAddress();
  OnThemeEditorDismissed();
  OnTabPreviewDismissed(store_.ui().tab_preview.request_id);
  if (store_.ui().settings.open)
    OnSettingsDismissed();
  while (!menu_anchors_.empty())
    DismissMenu(menu_anchors_.begin()->first);
  store_.OpenWorkspaceEditor(std::move(value), can_remove);
  ScheduleRender();
  return {};
}

Result<void> Shell::OnWorkspaceSubmitted(WorkspaceEditKind kind,
                                         std::string_view name,
                                         std::string_view glyph) {
  const auto& state = store_.ui().workspace_editor;
  if (state.open && state.icon_tab) {
    if (!browser_.CanSetTabIcon() || !store_.FindTab(state.icon_tab) ||
        glyph.size() > 128 || glyph.contains('\0') ||
        (kind != WorkspaceEditKind::kUpsert &&
         kind != WorkspaceEditKind::kRemove))
      return Fail(ErrorCode::kInvalidArgument, "Icon edit is unavailable");
    if (kind == WorkspaceEditKind::kUpsert) {
      const auto value = PrepareWorkspaceEdit(
          WorkspaceEditKind::kUpsert, nullptr, false, "Icon", glyph, {});
      if (!value)
        return std::unexpected(value.error());
    }
    const auto tab = state.icon_tab;
    store_.CloseWorkspaceEditor();
    ScheduleRender();
    browser_.SetTabIcon(
        tab, kind == WorkspaceEditKind::kRemove ? std::string_view{} : glyph);
    return {};
  }

  if (!state.open || !browser_.CanEditWorkspaces() ||
      (kind != WorkspaceEditKind::kUpsert &&
       kind != WorkspaceEditKind::kRemove) ||
      (kind == WorkspaceEditKind::kRemove &&
       (!state.can_remove || store_.spaces().empty() ||
        store_.spaces().front().id == state.value.id)))
    return Fail(ErrorCode::kInvalidArgument, "Workspace edit is unavailable");
  const auto value = PrepareWorkspaceEdit(
      kind, state.value.id ? &state.value : nullptr, state.can_remove, name,
      glyph, store_.theme().accent);
  if (!value)
    return std::unexpected(value.error());
  store_.CloseWorkspaceEditor();
  ScheduleRender();
  browser_.EditWorkspace(kind, *value);
  return {};
}

void Shell::OnWorkspaceEditorDismissed() {
  store_.CloseWorkspaceEditor();
  ScheduleRender();
}

}  // namespace kr
