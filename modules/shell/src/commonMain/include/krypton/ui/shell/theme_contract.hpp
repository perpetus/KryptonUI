#pragma once

#include "krypton/ui/core/ids.hpp"
#include "krypton/ui/core/result.hpp"
#include "krypton/ui/model/workspace_theme.hpp"
#include "krypton_ui/abi.h"

namespace kr {
[[nodiscard]] Result<WorkspaceTheme> FromThemeContract(
    const KrWorkspaceTheme* theme);
[[nodiscard]] KrWorkspaceTheme ToThemeContract(
    SpaceId space,
    const WorkspaceTheme& theme) noexcept;
}  // namespace kr
