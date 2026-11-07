#include "krypton/ui/state/store.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <utility>

#include "krypton/ui/model/workspace_symbols.hpp"

namespace kr {

Result<void> Store::SetUiText(std::string_view key, std::string_view text) {
  for (std::size_t index = 0; index < kUiTextDefaults.size(); ++index) {
    if (kUiTextDefaults[index].key != key)
      continue;
    if (text_[index] != text) {
      text_[index] = text;
      MarkDirty(Dirty::kText);
    }
    return {};
  }
  return Fail(ErrorCode::kInvalidArgument, "Unknown UI text key");
}

void Store::SetTextDirection(bool right_to_left) {
  if (right_to_left_ != right_to_left) {
    right_to_left_ = right_to_left;
    MarkDirty(Dirty::kText);
  }
}

void Store::SetNativeTitleBar(const NativeTitleBarMetrics& metrics) {
  if (ui_.native_titlebar != metrics) {
    ui_.native_titlebar = metrics;
    MarkDirty(Dirty::kLayout);
  }
}

Result<void> Store::InsertTab(std::size_t index, Tab tab) {
  if (const auto* folder = FindFolder(tab.folder);
      tab.folder && (!folder || folder->space != tab.space ||
                     tab.flags.Has(TabFlag::kFavorite)))
    tab.folder = {};
  if (!tab.id) {
    return Fail(ErrorCode::kInvalidArgument, "tab id 0 is reserved");
  }
  if (index_.contains(tab.id)) {
    return Fail(ErrorCode::kInvalidArgument,
                std::format("tab {} is already present", tab.id));
  }
  if (index > tabs_.size()) {
    return Fail(ErrorCode::kInvalidArgument,
                std::format("insert index {} is past the end ({})", index,
                            tabs_.size()));
  }
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(index),
               std::move(tab));
  ReindexFrom(index);
  MarkDirty(Dirty::kTabs);
  return {};
}

Result<void> Store::UpdateTab(Tab tab) {
  if (const auto* folder = FindFolder(tab.folder);
      tab.folder && (!folder || folder->space != tab.space ||
                     tab.flags.Has(TabFlag::kFavorite)))
    tab.folder = {};
  const auto it = index_.find(tab.id);
  if (it == index_.end()) {
    return Fail(ErrorCode::kNotFound, std::format("no tab {}", tab.id));
  }
  tabs_[it->second] = std::move(tab);
  MarkDirty(Dirty::kTabs);
  return {};
}

Result<void> Store::MoveTab(TabId id, std::size_t to_index) {
  const auto from = IndexOf(id);
  if (!from) {
    return Fail(ErrorCode::kNotFound, std::format("no tab {}", id));
  }
  if (to_index >= tabs_.size()) {
    return Fail(ErrorCode::kInvalidArgument,
                std::format("move index {} is past the end ({})", to_index,
                            tabs_.size()));
  }
  if (*from == to_index)
    return {};

  // Rotate instead of erase + insert: one pass, no reallocation.
  const auto begin = tabs_.begin();
  if (*from < to_index) {
    std::rotate(begin + *from, begin + *from + 1, begin + to_index + 1);
  } else {
    std::rotate(begin + to_index, begin + *from, begin + *from + 1);
  }
  ReindexFrom(std::min(*from, to_index));
  MarkDirty(Dirty::kTabs);
  return {};
}

Result<void> Store::RemoveTab(TabId id) {
  const auto index = IndexOf(id);
  if (!index) {
    return Fail(ErrorCode::kNotFound, std::format("no tab {}", id));
  }
  if (ui_.workspace_editor.open && ui_.workspace_editor.icon_tab == id)
    CloseWorkspaceEditor();
  if (ui_.boost_editor.open && ui_.boost_editor.draft.tab == id)
    CloseBoostEditor();
  ForgetMultiPageTab(id);
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(*index));
  if (ui_.tab_preview.tab == id)
    EndTabPreview();
  index_.erase(id);
  ReindexFrom(*index);
  if (active_tab_ == id) {
    active_tab_ = {};
    loading_.Reset({});
    MarkDirty(Dirty::kLoading);
    MarkDirty(Dirty::kActiveTab);
  }
  MarkDirty(Dirty::kTabs);
  return {};
}

Result<void> Store::SetActiveTab(TabId id) {
  if (id && !index_.contains(id)) {
    return Fail(ErrorCode::kNotFound, std::format("no tab {}", id));
  }
  if (active_tab_ != id) {
    active_tab_ = id;
    loading_.Reset(id);
    MarkDirty(Dirty::kActiveTab | Dirty::kLoading);
  }
  return {};
}

void Store::SetSpaces(std::vector<Space> spaces, SpaceId active) {
  for (auto& space : spaces)
    if (space.glyph.empty())
      space.glyph = kDefaultWorkspaceSymbol;
  spaces_ = std::move(spaces);
  active_space_ = active;
  // The active space decides which tabs are listed, so tabs change too.
  MarkDirty(Dirty::kSpaces | Dirty::kTabs | Dirty::kTheme);
}

void Store::SetNav(NavState nav) {
  loading_.SyncNavigation(active_tab_, nav.is_loading,
                          !nav_.is_loading || nav.full_url != nav_.full_url,
                          IsProgressPage(nav.full_url));
  nav_ = std::move(nav);
  MarkDirty(Dirty::kNav | Dirty::kLoading);
}

void Store::SetActions(std::vector<Action> actions) {
  actions_ = std::move(actions);
  MarkDirty(Dirty::kActions);
}

void Store::SetTheme(const Theme& theme) {
  theme_ = theme;
  MarkDirty(Dirty::kTheme);
}

void Store::SetWindow(const WindowState& window) {
  window_ = window;
  MarkDirty(Dirty::kWindow);
}

void Store::SetLayoutPrefs(const LayoutPrefs& prefs) {
  if (prefs_.flags.Has(LayoutFlag::kWorkspaceSwipe) !=
      prefs.flags.Has(LayoutFlag::kWorkspaceSwipe))
    ui_.workspace_swipe.Reset();
  if (prefs_.flags != prefs.flags) {
    ui_.sidebar_revealed = false;
    ui_.toolbar_revealed = false;
  }
  prefs_ = prefs;
  MarkDirty(Dirty::kLayout);
}

bool Store::PutImage(ImageId id,
                     int width,
                     int height,
                     int stride,
                     const std::uint8_t* pixels) {
  if (!images_.Put(id, width, height, stride, pixels))
    return false;
  MarkDirty(Dirty::kImages);
  return true;
}

void Store::DropImage(ImageId id) {
  images_.Drop(id);
  MarkDirty(Dirty::kImages);
}

void Store::OpenCommandBar(std::string text, bool select_all) {
  CommandBarState& bar = ui_.command_bar;
  bar.open = true;
  bar.pointer_open = false;
  bar.typed = false;
  bar.text = std::move(text);
  bar.select_all = select_all;
  const auto length = AddressTextLength(bar.text).value_or(0);
  bar.selection = {select_all ? 0 : length, length, false};
  ++bar.edit_revision;
  bar.suggestions.clear();
  bar.selected = 0;
  bar.request_id = 0;
  bar.generation = 0;
  ++bar.open_serial;
  MarkDirty(Dirty::kCommandBar);
}

void Store::SetCommandPointerOrigin() {
  if (!ui_.command_bar.open)
    return;
  ui_.command_bar.pointer_open = true;
  MarkDirty(Dirty::kCommandBar);
}

void Store::MarkCommandTyped() {
  if (!ui_.command_bar.typed) {
    ui_.command_bar.typed = true;
    MarkDirty(Dirty::kCommandBar);
  }
}

void Store::CloseCommandBar() {
  CommandBarState& bar = ui_.command_bar;
  if (!bar.open)
    return;
  bar.open = false;
  bar.request_id = 0;
  bar.generation = 0;
  bar.suggestions.clear();
  MarkDirty(Dirty::kCommandBar);
}

std::uint32_t Store::SetCommandText(std::string text) {
  CommandBarState& bar = ui_.command_bar;
  bar.text = std::move(text);
  bar.generation = 0;
  bar.select_all = false;
  // Results belong to the previous request. They must not be accepted using
  // the new request id while the host is still preparing its reply.
  bar.suggestions.clear();
  bar.selected = 0;
  bar.request_id = next_request_id_++;
  if (next_request_id_ == 0)
    ++next_request_id_;
  MarkDirty(Dirty::kCommandBar);
  return bar.request_id;
}

void Store::SetCommandEdit(std::string text,
                           AddressSelection selection,
                           bool native) {
  auto& bar = ui_.command_bar;
  bar.text = std::move(text);
  bar.selection = selection;
  bar.select_all = false;
  if (native)
    ++bar.edit_revision;
  MarkDirty(Dirty::kCommandBar);
}

void Store::SetSuggestions(std::uint32_t request_id,
                           std::vector<Suggestion> suggestions,
                           std::uint64_t generation) {
  CommandBarState& bar = ui_.command_bar;
  if (!bar.open || request_id == 0 || request_id != bar.request_id)
    return;
  bar.suggestions = std::move(suggestions);
  bar.generation = generation;
  auto preferred = std::ranges::find_if(
      bar.suggestions, [](const Suggestion& s) { return s.selected; });
  if (preferred == bar.suggestions.end())
    preferred = std::ranges::find_if(
        bar.suggestions, [](const Suggestion& s) { return s.is_default; });
  bar.selected = preferred == bar.suggestions.end()
                     ? 0
                     : static_cast<int>(preferred - bar.suggestions.begin());
  MarkDirty(Dirty::kCommandBar);
}

void Store::OpenWorkspaceEditor(Space value, bool can_remove, TabId icon_tab) {
  auto& editor = ui_.workspace_editor;
  editor.value = std::move(value);
  editor.icon_tab = icon_tab;
  editor.can_remove = can_remove;
  editor.open = true;
  ++editor.serial;
  MarkDirty(Dirty::kWorkspaceEditor);
}

void Store::CloseWorkspaceEditor() {
  if (!ui_.workspace_editor.open)
    return;
  ui_.workspace_editor.open = false;
  MarkDirty(Dirty::kWorkspaceEditor);
}

void Store::MoveCommandSelection(int delta) {
  CommandBarState& bar = ui_.command_bar;
  if (bar.suggestions.empty())
    return;
  const int count = static_cast<int>(bar.suggestions.size());
  bar.selected = ((bar.selected + delta) % count + count) % count;
  MarkDirty(Dirty::kCommandBar);
}

void Store::SetCommandSelection(std::size_t index) {
  auto& bar = ui_.command_bar;
  if (index == std::numeric_limits<std::size_t>::max()) {
    if (bar.selected != -1) {
      bar.selected = -1;
      MarkDirty(Dirty::kCommandBar);
    }
    return;
  }
  if (index >= bar.suggestions.size() ||
      index == static_cast<std::size_t>(bar.selected))
    return;
  bar.selected = static_cast<int>(index);
  MarkDirty(Dirty::kCommandBar);
}

void Store::SetSidebarRevealed(bool revealed) {
  if (ui_.sidebar_revealed == revealed)
    return;
  ui_.sidebar_revealed = revealed;
  MarkDirty(Dirty::kLayout);
}

void Store::BeginTabPreview(TabId tab, std::uint32_t request_id) {
  ui_.tab_preview = {.tab = tab, .request_id = request_id};
  MarkDirty(Dirty::kTabPreview);
}

void Store::EndTabPreview() {
  if (!ui_.tab_preview.tab)
    return;
  ui_.tab_preview = {};
  MarkDirty(Dirty::kTabPreview);
}

bool Store::SetTabPreview(std::uint32_t request_id,
                          TabId tab,
                          const TabPreviewData& data) {
  auto& preview = ui_.tab_preview;
  if (!tab || preview.tab != tab || preview.request_id != request_id ||
      preview.data == data)
    return false;
  preview.data = data;
  MarkDirty(Dirty::kTabPreview);
  return true;
}

void Store::SetToolbarRevealed(bool revealed) {
  if (ui_.toolbar_revealed == revealed)
    return;
  ui_.toolbar_revealed = revealed;
  MarkDirty(Dirty::kLayout);
}

const Tab* Store::FindTab(TabId id) const {
  const auto index = IndexOf(id);
  return index ? &tabs_[*index] : nullptr;
}

std::optional<std::size_t> Store::IndexOf(TabId id) const {
  const auto it = index_.find(id);
  if (it == index_.end())
    return std::nullopt;
  return it->second;
}

const Space* Store::FindSpace(SpaceId id) const {
  const auto it = std::ranges::find(spaces_, id, &Space::id);
  return it == spaces_.end() ? nullptr : &*it;
}

Flags<Dirty> Store::TakeDirty() {
  return std::exchange(dirty_, Flags<Dirty>{});
}

void Store::ReindexFrom(std::size_t first) {
  for (std::size_t i = first; i < tabs_.size(); ++i) {
    index_[tabs_[i].id] = i;
  }
}

}  // namespace kr
