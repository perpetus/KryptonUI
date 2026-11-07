#include <algorithm>

#include "krypton/fixtures/mock_browser.hpp"

namespace kr::fixtures {
void MockBrowser::ShowWorkspaceEditor(KrSpaceId space) {
  if (attached_ && KR_HAS_SLOT(&ui_, KrShellUI, show_workspace_editor) &&
      ui_.show_workspace_editor)
    ui_.show_workspace_editor(ui_.self, space);
}

void MockBrowser::EditWorkspace(std::uint32_t kind, const KrSpace& value) {
  if (spaces_.empty() || kind > KR_WORKSPACE_REMOVE)
    return;
  auto found = std::ranges::find(spaces_, value.id, &MockSpace::id);
  if (kind == KR_WORKSPACE_REMOVE) {
    if (found == spaces_.end() || found == spaces_.begin())
      return;
    const auto fallback = spaces_.front().id;
    spaces_.erase(found);
    std::erase_if(extra_workspace_themes_, [&](const KrWorkspaceTheme& theme) {
      return theme.space == value.id;
    });
    for (auto& tab : tabs_) {
      if (tab.space == value.id) {
        tab.space = fallback;
        PushTab(tab);
      }
    }
    if (active_space_ == value.id)
      SwitchSpace(fallback);
  } else {
    if (!value.name.data || !value.name.size ||
        (value.id && found == spaces_.end()))
      return;
    MockSpace updated{value.id, std::string(value.name.data, value.name.size),
                      value.glyph.size
                          ? std::string(value.glyph.data, value.glyph.size)
                          : std::string{},
                      value.color};
    if (value.id) {
      updated.last_active = found->last_active;
      *found = std::move(updated);
    } else {
      // Fixture IDs are never reused while the preview process is alive.
      updated.id = next_workspace_id_++;
      spaces_.push_back(std::move(updated));
    }
  }
  PushSpaces();
  PushTheme();
  if (callbacks_.page_changed)
    callbacks_.page_changed();
}
}  // namespace kr::fixtures
