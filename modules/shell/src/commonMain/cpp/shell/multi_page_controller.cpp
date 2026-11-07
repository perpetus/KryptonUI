#include "krypton/ui/shell/shell.hpp"

namespace kr {
std::optional<TabItemVm> Shell::OnSplitCandidateRequested(TabId id) const {
  if (const auto* tab = store_.FindTab(id))
    return PresentTabItem(*tab, store_.presented_tab());
  return std::nullopt;
}
Result<void> BrowserPort::MultiPageCommand(
    const KrMultiPageCommand& command) const {
  if (!CanMultiPage())
    return Fail(ErrorCode::kUnsupported, "Multi-page commands are unavailable");
  const auto result = table_.multi_page_command(table_.self, &command);
  if (result != KR_OK)
    return Fail(result == KR_ERR_UNSUPPORTED ? ErrorCode::kUnsupported
                                             : ErrorCode::kInvalidArgument,
                "Multi-page command was rejected");
  return {};
}
Result<void> Shell::ShowSplitChooser() {
  DismissBoostEditor();
  if (!browser_.CanMultiPage() || OnboardingActive())
    return Fail(ErrorCode::kUnsupported, "Split chooser is unavailable");
  auto result = store_.OpenSplitChooser();
  if (result) {
    CloseAddress();
    ScheduleRender();
  }
  return result;
}
Result<void> Shell::ShowSplitChooser(SplitLayout layout) {
  if (!IsValidSplitLayout(layout) ||
      (IsSnapSplitLayout(layout) && !browser_.CanSnapLayouts()))
    return Fail(ErrorCode::kUnsupported, "Split layout unavailable");
  auto result = ShowSplitChooser();
  if (result)
    store_.SetSplitChooserLayout(layout);
  return result;
}
bool Shell::OnSplitSelectionChanged(std::span<const TabId> tabs) {
  const auto result = store_.SetSplitSelection(tabs);
  if (result)
    ScheduleRender();
  return result.has_value();
}

Result<void> Shell::ShowSplitOptions(TabId tab, Rect anchor) {
  if (!browser_.CanMultiPage() || OnboardingActive())
    return Fail(ErrorCode::kUnsupported, "Split options unavailable");
  auto result = store_.OpenSplitOptions(tab, anchor);
  if (result) {
    CloseAddress();
    ScheduleRender();
  }
  return result;
}
void Shell::OnSplitLayoutSelected(SplitLayout layout) {
  if (!IsValidSplitLayout(layout))
    return;
  if (IsSnapSplitLayout(layout) && !browser_.CanSnapLayouts())
    return;
  const auto selected = store_.ui().split_chooser;
  if (selected.open && selected.layout_only) {
    const auto* group = store_.SplitForTab(selected.target);
    if (group && group->id == selected.group &&
        group->revision == selected.group_revision) {
      const KrSplitEdit edit{sizeof(KrSplitEdit),
                             KR_SPLIT_SET_LAYOUT,
                             group->revision,
                             selected.target.value,
                             group->space.value,
                             0,
                             static_cast<KrSplitLayout>(layout),
                             0.5f};
      const KrMultiPageCommand command{
          .struct_size = sizeof(KrMultiPageCommand),
          .kind = KR_MULTI_PAGE_EDIT_SPLIT,
          .tab = selected.target.value,
          .group = group->id.value,
          .revision = group->revision,
          .edit = &edit};
      if (const auto result = browser_.MultiPageCommand(command); !result)
        return;
    }
    OnSplitChooserDismissed();
    return;
  }
  store_.SetSplitChooserLayout(layout);
  ScheduleRender();
}
void Shell::OnSplitChooserDismissed() {
  store_.CloseSplitChooser();
  ScheduleRender();
}
bool Shell::OnSplitChooserAccepted() {
  const auto selected = store_.ui().split_chooser;
  if (!selected.open || selected.count < 2)
    return false;
  if (IsSnapSplitLayout(selected.layout) && !browser_.CanSnapLayouts())
    return false;
  std::array<KrSplitMember, kMaxSplitMembers> members{};
  for (std::size_t i = 0; i < selected.count; ++i)
    members[i] = {sizeof(KrSplitMember), selected.selected[i].value};
  const KrMultiPageCommand command{
      .struct_size = sizeof(KrMultiPageCommand),
      .kind = KR_MULTI_PAGE_NEW_SPLIT,
      .tab = store_.presented_tab().value,
      .members = members.data(),
      .member_count = selected.count,
      .member_size = sizeof(KrSplitMember),
      .layout = static_cast<KrSplitLayout>(selected.layout)};
  const auto result = browser_.MultiPageCommand(command);
  if (result)
    OnSplitChooserDismissed();
  return result.has_value();
}
void Shell::OnSplitPaneActivated(TabId tab) {
  browser_.ActivateTab(tab);
}
std::optional<SplitRowVm> Shell::OnSplitRowRequested(TabId tab) const {
  return PresentSplitRow(store_, tab);
}
Result<void> Shell::UpsertSplitGroup(const SplitGroup& group) {
  if (!browser_.CanMultiPage() ||
      (IsSnapSplitLayout(group.layout) && !browser_.CanSnapLayouts()))
    return Fail(ErrorCode::kUnsupported,
                "Host lacks complete multi-page support");
  auto result = store_.UpsertSplitGroup(group);
  if (result)
    ScheduleRender();
  return result;
}
Result<void> Shell::RemoveSplitGroup(SplitGroupId id, std::uint64_t revision) {
  if (!browser_.CanMultiPage())
    return Fail(ErrorCode::kUnsupported,
                "Host lacks complete multi-page support");
  auto result = store_.RemoveSplitGroup(id, revision);
  if (result)
    ScheduleRender();
  return result;
}
Result<void> Shell::SetGlance(const GlanceState& state) {
  if (!browser_.CanMultiPage())
    return Fail(ErrorCode::kUnsupported,
                "Host lacks complete multi-page support");
  auto result = store_.SetGlance(state);
  if (result)
    ScheduleRender();
  return result;
}
}  // namespace kr
