#include <algorithm>
#include <string>
#include <vector>

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

KrProfileId MockBrowser::ResolveProfile(KrProfileId profile,
                                        std::string_view new_profile) {
  if (!new_profile.empty()) {
    profiles_.emplace_back(next_profile_id_, std::string(new_profile));
    return next_profile_id_++;
  }
  return std::ranges::any_of(
             profiles_, [profile](const auto& entry) { return entry.first == profile; })
             ? profile
             : 0;
}

KrResult MockBrowser::SetSpaceProfile(KrSpaceId space,
                                      KrProfileId profile,
                                      std::string_view new_profile) {
  const auto found = std::ranges::find(spaces_, space, &MockSpace::id);
  if (found == spaces_.end() || found == spaces_.begin())
    return KR_ERR_INVALID_ARGUMENT;
  // Mirrors the host rule: a Space with tabs keeps its profile.
  if (std::ranges::any_of(tabs_,
                          [space](const auto& tab) { return tab.space == space; }))
    return KR_ERR_INVALID_ARGUMENT;
  const auto resolved = ResolveProfile(profile, new_profile);
  if (!resolved)
    return KR_ERR_INVALID_ARGUMENT;
  found->profile = resolved;
  PushSpaceProfiles();
  return KR_OK;
}

KrResult MockBrowser::CreateSpace(const KrSpace& space,
                                  KrProfileId profile,
                                  std::string_view new_profile) {
  if (space.id || !space.name.data || !space.name.size)
    return KR_ERR_INVALID_ARGUMENT;
  const auto resolved = ResolveProfile(profile, new_profile);
  if (!resolved)
    return KR_ERR_INVALID_ARGUMENT;
  EditWorkspace(KR_WORKSPACE_UPSERT, space);
  spaces_.back().profile = resolved;
  PushSpaceProfiles();
  SwitchSpace(spaces_.back().id);
  return KR_OK;
}

void MockBrowser::PushSpaceProfiles() const {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, set_space_profiles) ||
      !ui_.set_space_profiles)
    return;
  std::vector<KrProfileChoice> choices;
  for (const auto& [id, name] : profiles_)
    choices.push_back({sizeof(KrProfileChoice), id, {name.data(), name.size()},
                       id == kCurrentProfile ? KR_PROFILE_CURRENT : 0u});
  std::vector<KrSpaceProfile> spaces;
  for (const auto& space : spaces_) {
    const bool used = std::ranges::any_of(
        tabs_, [&](const auto& tab) { return tab.space == space.id; });
    spaces.push_back({sizeof(KrSpaceProfile), space.id, space.profile,
                      KR_SPACE_PROFILE_READY,
                      used ? KR_SPACE_PROFILE_IN_USE : 0u});
  }
  ui_.set_space_profiles(ui_.self, choices.data(), choices.size(),
                         sizeof(KrProfileChoice), spaces.data(), spaces.size(),
                         sizeof(KrSpaceProfile));
}
}  // namespace kr::fixtures
