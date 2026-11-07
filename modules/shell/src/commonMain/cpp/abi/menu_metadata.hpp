#pragma once

#include <optional>

#include "krypton/ui/core/result.hpp"
#include "krypton/ui/model/menu_metadata.hpp"
#include "krypton_ui/abi.h"

namespace kr::abi {

[[nodiscard]] Result<std::optional<MenuItemMetadata>> ReadMenuMetadata(
    const KrMenuItemMetadata* input);

}  // namespace kr::abi
