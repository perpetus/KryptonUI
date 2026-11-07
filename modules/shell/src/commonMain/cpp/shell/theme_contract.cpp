#include "krypton/ui/shell/theme_contract.hpp"

#include <cstddef>

#include "krypton/ui/presentation/theme_editor.hpp"

namespace kr {
Result<WorkspaceTheme> FromThemeContract(const KrWorkspaceTheme* input) {
  if (!input || input->struct_size <
                    offsetof(KrWorkspaceTheme, points) + sizeof(input->points))
    return Fail(ErrorCode::kInvalidArgument, "incomplete workspace theme");
  WorkspaceTheme result{.customized = (input->flags & KR_THEME_CUSTOMIZED) != 0,
                        .scheme = static_cast<ThemeScheme>(input->scheme),
                        .harmony = static_cast<ThemeHarmony>(input->harmony),
                        .mode = static_cast<ThemeColorMode>(input->mode),
                        .point_count = input->point_count,
                        .opacity = input->opacity,
                        .texture = input->texture,
                        .lightness = input->lightness};
  for (std::size_t i = 0; i < kMaxThemePoints; ++i)
    result.points[i] = {{input->points[i].x, input->points[i].y},
                        Color{input->points[i].color}};
  if (!ValidWorkspaceTheme(result))
    return Fail(ErrorCode::kInvalidArgument, "invalid workspace theme fields");
  return result;
}

KrWorkspaceTheme ToThemeContract(SpaceId space,
                                 const WorkspaceTheme& theme) noexcept {
  KrWorkspaceTheme result{.struct_size = sizeof(KrWorkspaceTheme),
                          .space = space.value,
                          .flags = theme.customized ? KR_THEME_CUSTOMIZED : 0u,
                          .scheme = static_cast<std::uint32_t>(theme.scheme),
                          .harmony = static_cast<std::uint32_t>(theme.harmony),
                          .mode = static_cast<std::uint32_t>(theme.mode),
                          .point_count = theme.point_count,
                          .opacity = theme.opacity,
                          .texture = theme.texture,
                          .lightness = theme.lightness};
  for (std::size_t i = 0; i < kMaxThemePoints; ++i)
    result.points[i] = {theme.points[i].position.x, theme.points[i].position.y,
                        theme.points[i].color.argb};
  return result;
}
}  // namespace kr
