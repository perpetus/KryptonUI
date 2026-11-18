#pragma once

#include <cstddef>

#include "krypton/ui/design/tokens.hpp"

namespace kr {
// Root rows keep the standard inset; every folder level adds a visible step.
[[nodiscard]] constexpr float FolderRowInset(
    std::size_t depth,
    const design::Tokens& tokens = design::kTokens) noexcept {
  return tokens.spacing.tab_padding +
         tokens.folder.indent * static_cast<float>(depth);
}
}  // namespace kr
