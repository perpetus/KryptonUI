#pragma once

#include "krypton/ui/model/multi_page.hpp"
#include "krypton_ui/abi.h"

namespace kr::abi {
[[nodiscard]] MultiPageResult<SplitGroup> ReadSplit(const KrSplitGroup* input);
[[nodiscard]] MultiPageResult<GlanceState> ReadGlance(
    const KrGlanceState* input) noexcept;
void FillMultiPageUi(KrShellUI& ui) noexcept;
}  // namespace kr::abi
