#pragma once

#include <cstddef>

#include "krypton/ui/design/tokens.hpp"

namespace kr {

[[nodiscard]] std::size_t FavoriteColumns(
    float width,
    std::size_t count,
    const design::Tokens& tokens = design::kTokens) noexcept;

}  // namespace kr
