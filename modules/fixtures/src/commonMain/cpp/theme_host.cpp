#include <algorithm>
#include <cmath>
#include <cstddef>

#include "krypton/fixtures/mock_browser.hpp"
#include "krypton/ui/design/tokens.generated.hpp"

namespace kr::fixtures {

void MockBrowser::PushWorkspaceThemes() const {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, set_workspace_theme) ||
      !ui_.set_workspace_theme)
    return;
  for (std::size_t i = 0; i < options_.appearance.workspace_themes.size();
       ++i) {
    if (std::ranges::find(spaces_, i + 1, &MockSpace::id) == spaces_.end())
      continue;
    auto theme = options_.appearance.workspace_themes[i];
    if (!theme.struct_size) {
      theme.struct_size = sizeof(theme);
      theme.opacity = design::kTokens.theme.opacity;
      theme.lightness = 50;
    }
    theme.space = i + 1;
    theme.scheme = static_cast<std::uint32_t>(options_.appearance.scheme);
    ui_.set_workspace_theme(ui_.self, &theme);
  }
  for (auto theme : extra_workspace_themes_) {
    theme.scheme = static_cast<std::uint32_t>(options_.appearance.scheme);
    ui_.set_workspace_theme(ui_.self, &theme);
  }
}

void MockBrowser::ShowThemeEditor(KrSpaceId space) {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, show_theme_editor) ||
      !ui_.show_theme_editor)
    return;
  PushWorkspaceThemes();
  ui_.show_theme_editor(ui_.self, space ? space : active_space_);
}

void MockBrowser::ResetWorkspaceTheme(KrSpaceId space) {
  if (!space ||
      std::ranges::find(spaces_, space, &MockSpace::id) == spaces_.end())
    return;
  KrWorkspaceTheme* target = nullptr;
  if (space <= options_.appearance.workspace_themes.size()) {
    target = &options_.appearance.workspace_themes[space - 1];
  } else {
    auto found = std::ranges::find(extra_workspace_themes_, space,
                                   &KrWorkspaceTheme::space);
    if (found == extra_workspace_themes_.end()) {
      extra_workspace_themes_.emplace_back();
      target = &extra_workspace_themes_.back();
    } else {
      target = &*found;
    }
  }
  auto& theme = *target;
  theme = {};
  theme.struct_size = sizeof(theme);
  theme.space = space;
  theme.scheme = static_cast<std::uint32_t>(options_.appearance.scheme);
  theme.opacity = design::kTokens.theme.opacity;
  theme.lightness = 50;
}

void MockBrowser::ApplyWorkspaceTheme(const KrWorkspaceTheme& input,
                                      bool commit) {
  if (input.struct_size <
          offsetof(KrWorkspaceTheme, points) + sizeof(input.points) ||
      !input.space ||
      std::ranges::find(spaces_, input.space, &MockSpace::id) ==
          spaces_.end() ||
      input.point_count > 3 || input.scheme > KR_THEME_DARK ||
      input.harmony > KR_THEME_TRIADIC ||
      (input.flags & ~KR_THEME_CUSTOMIZED) || input.mode > KR_THEME_GRAYSCALE ||
      !(input.opacity >= design::kTokens.theme.min_opacity &&
        input.opacity <= design::kTokens.theme.max_opacity) ||
      !(input.texture >= 0 && input.texture <= 1) ||
      !(input.lightness >= 0 && input.lightness <= 100))
    return;
  for (std::size_t i = 0; i < input.point_count; ++i)
    if (!(input.points[i].x >= 0 && input.points[i].x <= 1 &&
          input.points[i].y >= 0 && input.points[i].y <= 1))
      return;
  if (input.space <= options_.appearance.workspace_themes.size()) {
    options_.appearance.workspace_themes[input.space - 1] = input;
    options_.appearance.workspace_colors[input.space - 1] =
        input.point_count ? input.points[0].color : 0;
  } else {
    const auto found = std::ranges::find(extra_workspace_themes_, input.space,
                                         &KrWorkspaceTheme::space);
    if (found == extra_workspace_themes_.end())
      extra_workspace_themes_.push_back(input);
    else
      *found = input;
    const auto space = std::ranges::find(spaces_, input.space, &MockSpace::id);
    if (input.point_count)
      space->color = input.points[0].color;
  }
  options_.appearance.scheme = static_cast<ThemeScheme>(input.scheme);
  options_.dark = options_.appearance.scheme == ThemeScheme::kSystem
                      ? system_dark_
                      : options_.appearance.scheme == ThemeScheme::kDark;
  for (auto& space : spaces_) {
    if (space.id && space.id <= options_.appearance.workspace_colors.size())
      space.color = options_.appearance.workspace_colors[space.id - 1];
  }
  PushTheme();
  PushWorkspaceThemes();
  PushSpaces();
  if (callbacks_.page_changed)
    callbacks_.page_changed();
  if (commit && callbacks_.appearance_changed)
    callbacks_.appearance_changed(options_.appearance);
}

}  // namespace kr::fixtures
