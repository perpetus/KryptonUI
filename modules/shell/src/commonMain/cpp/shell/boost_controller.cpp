#include "../abi/boost_abi.hpp"
#include "krypton/ui/shell/shell.hpp"

namespace kr {
bool BrowserPort::EditBoost(BoostEdit kind, const BoostDraft& value) const {
  if (!KR_HAS_SLOT(&table_, KrBrowser, edit_boost) || !table_.edit_boost)
    return false;
  const auto compiled = BoostCss(value);
  const auto draft = abi::WriteBoost(value, compiled);
  return table_.edit_boost(table_.self, static_cast<std::uint32_t>(kind),
                           &draft) == KR_OK;
}
void Shell::DismissBoostEditor() {
  const auto& editor = store_.ui().boost_editor;
  if (!editor.open)
    return;
  if (!browser_.EditBoost(BoostEdit::kCommit, editor.draft))
    (void)browser_.EditBoost(BoostEdit::kCancel, editor.draft);
  store_.CloseBoostEditor();
  ScheduleRender();
}
Result<void> Shell::ShowBoostEditor(BoostDraft value) {
  if (OnboardingActive() || !store_.FindTab(value.tab) || !ValidBoost(value))
    return Fail(ErrorCode::kInvalidArgument, "Boost editing is unavailable");
  OnLibraryDismissed();
  if (store_.ui().settings.open)
    OnSettingsDismissed();
  CloseAddress();
  OnThemeEditorDismissed();
  OnWorkspaceEditorDismissed();
  OnTabPreviewDismissed(store_.ui().tab_preview.request_id);
  if (!menu_anchors_.empty())
    DismissMenu(menu_anchors_.begin()->first);
  if (value.auto_theme) {
    if (const auto* theme = store_.WorkspaceThemeFor(store_.active_space());
        theme && theme->point_count)
      value.accent = theme->points[0].color;
    else if (const auto* space = store_.FindSpace(store_.active_space()))
      value.accent = space->color;
    if (!value.accent.a())
      value.accent = store_.theme().accent;
  }
  store_.OpenBoostEditor(std::move(value));
  (void)browser_.EditBoost(BoostEdit::kPreview, store_.ui().boost_editor.draft);
  ScheduleRender();
  return {};
}
bool Shell::OnBoostEdited(BoostEdit kind, const BoostDraft& value) {
  const auto& editor = store_.ui().boost_editor;
  if (!editor.open || editor.draft.serial != value.serial ||
      editor.draft.tab != value.tab || editor.draft.id != value.id ||
      !ValidBoost(value) || !browser_.EditBoost(kind, value))
    return false;
  if (kind == BoostEdit::kCommit || kind == BoostEdit::kCancel ||
      kind == BoostEdit::kRemove)
    store_.CloseBoostEditor();
  else
    store_.UpdateBoostEditor(
        value, kind == BoostEdit::kZap || kind == BoostEdit::kFocus);
  ScheduleRender();
  return true;
}
}  // namespace kr
