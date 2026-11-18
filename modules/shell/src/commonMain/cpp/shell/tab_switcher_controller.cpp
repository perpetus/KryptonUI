#include <algorithm>
#include <array>
#include <limits>

#include "krypton/ui/presentation/tab_preview.hpp"
#include "krypton/ui/shell/shell.hpp"

namespace kr {
void Store::OpenTabSwitcher(std::vector<TabId> tabs, std::size_t selected) {
  ui_.tab_switcher = {.open = !tabs.empty(),
                      .tabs = std::move(tabs),
                      .selected = selected,
                      .request = 0,
                      .previews = {}};
  MarkDirty(Dirty::kTabSwitcher);
}
void Store::MoveTabSwitcher(int delta) {
  auto& state = ui_.tab_switcher;
  if (!state.open || state.tabs.empty())
    return;
  const auto count = static_cast<std::ptrdiff_t>(state.tabs.size());
  const auto position = static_cast<std::ptrdiff_t>(state.selected);
  state.selected =
      static_cast<std::size_t>((position + delta % count + count) % count);
  MarkDirty(Dirty::kTabSwitcher);
}
void Store::CloseTabSwitcher() {
  if (!ui_.tab_switcher.open)
    return;
  ui_.tab_switcher = {};
  MarkDirty(Dirty::kTabSwitcher);
}
void Store::BeginTabSwitcherPage(std::uint32_t request) {
  ui_.tab_switcher.request = request;
  ui_.tab_switcher.previews = {};
  MarkDirty(Dirty::kTabSwitcher);
}
bool Store::SetTabSwitcherPreview(std::uint32_t request,
                                  TabId tab,
                                  const TabPreviewData& data) {
  auto& state = ui_.tab_switcher;
  if (!state.open || request != state.request)
    return false;
  const auto first =
      state.selected / kTabSwitcherPageSize * kTabSwitcherPageSize;
  const auto count = std::min(kTabSwitcherPageSize, state.tabs.size() - first);
  for (std::size_t i = 0; i < count; ++i) {
    if (state.tabs[first + i] == tab) {
      if (state.previews[i] != data) {
        state.previews[i] = data;
        MarkDirty(Dirty::kTabSwitcher);
      }
      return true;
    }
  }
  return false;
}

void BrowserPort::RequestTabSwitcherPreviews(
    std::uint32_t request,
    std::span<const TabId> tabs) const {
  if (!KR_HAS_SLOT(&table_, KrBrowser, request_tab_switcher_previews) ||
      !table_.request_tab_switcher_previews)
    return;
  std::array<KrTabId, kTabSwitcherPageSize> ids{};
  const auto count = std::min(ids.size(), tabs.size());
  for (std::size_t i = 0; i < count; ++i)
    ids[i] = tabs[i].value;
  table_.request_tab_switcher_previews(table_.self, request, ids.data(), count);
}
void BrowserPort::CancelTabSwitcherPreviews(std::uint32_t request) const {
  if (request &&
      KR_HAS_SLOT(&table_, KrBrowser, cancel_tab_switcher_previews) &&
      table_.cancel_tab_switcher_previews)
    table_.cancel_tab_switcher_previews(table_.self, request);
}

bool Shell::OnTabSwitcherMoved(int delta) {
  const auto& ui = store_.ui();
  if (OnboardingActive() || ui.command_bar.open || ui.settings.open ||
      ui.theme_editor.open || ui.workspace_editor.open ||
      ui.boost_editor.open || ui.split_chooser.open || !menu_anchors_.empty())
    return false;
  if (!ui.tab_switcher.open) {
    std::vector<TabId> candidates;
    candidates.reserve(store_.tabs().size());
    const bool awake =
        store_.appearance_prefs().flags.Has(AppearanceFlag::kSwitcherAwakeOnly);
    const auto eligible = [&](const Tab& tab) {
      return !store_.IsTransientTab(tab.id) &&
             (!awake || !tab.flags.Has(TabFlag::kSleeping)) &&
             (tab.flags.Has(TabFlag::kFavorite) || !store_.active_space() ||
              tab.space == store_.active_space());
    };
    // At most 64 MRU identities are maintained on activation. Walk the full
    // tab collection only on demand, never on a render or an idle tick.
    for (const auto id : recent_tabs_)
      if (const auto* tab = store_.FindTab(id); tab && eligible(*tab))
        candidates.push_back(id);
    for (const auto& tab : store_.tabs())
      if (eligible(tab) &&
          std::ranges::find(recent_tabs_, tab.id) == recent_tabs_.end())
        candidates.push_back(tab.id);
    if (candidates.size() < 2)
      return true;
    OnTabPreviewDismissed(ui.tab_preview.request_id);
    OnLibraryDismissed();
    store_.OpenTabSwitcher(std::move(candidates), 0);
    store_.MoveTabSwitcher(delta);
    RequestTabSwitcherPage();
  } else {
    const auto previous_page = ui.tab_switcher.selected / kTabSwitcherPageSize;
    store_.MoveTabSwitcher(delta);
    if (previous_page != ui.tab_switcher.selected / kTabSwitcherPageSize)
      RequestTabSwitcherPage();
  }
  ScheduleRender();
  return true;
}
void Shell::RequestTabSwitcherPage() {
  const auto& state = store_.ui().tab_switcher;
  browser_.CancelTabSwitcherPreviews(state.request);
  if (++next_tab_preview_request_ == 0)
    ++next_tab_preview_request_;
  store_.BeginTabSwitcherPage(next_tab_preview_request_);
  const auto first =
      state.selected / kTabSwitcherPageSize * kTabSwitcherPageSize;
  browser_.RequestTabSwitcherPreviews(
      state.request, std::span<const TabId>(state.tabs)
                         .subspan(first, std::min(kTabSwitcherPageSize,
                                                  state.tabs.size() - first)));
}
void Shell::OnTabSwitcherAccepted(std::size_t index) {
  const auto& state = store_.ui().tab_switcher;
  if (!state.open)
    return;
  if (index == std::numeric_limits<std::size_t>::max())
    index = state.selected;
  if (index >= state.tabs.size())
    return;
  const auto tab = state.tabs[index];
  OnTabSwitcherDismissed();
  if (store_.FindTab(tab))
    OnTabActivated(tab);
}
void Shell::OnTabSwitcherDismissed() {
  const auto request = store_.ui().tab_switcher.request;
  if (!store_.ui().tab_switcher.open)
    return;
  store_.CloseTabSwitcher();
  browser_.CancelTabSwitcherPreviews(request);
  ScheduleRender();
}
}  // namespace kr
