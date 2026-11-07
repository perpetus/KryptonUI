#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "krypton/ui/model/model.hpp"

namespace kr {

// Where a dragged tab lands, as the final index in `tabs` that a host move
// takes: directly before `next`, or directly after `previous` when `next` is
// not listed. Nothing when neither neighbour is listed.
[[nodiscard]] std::optional<std::size_t> TabDropIndex(std::span<const Tab> tabs,
                                                      TabId tab,
                                                      TabId previous,
                                                      TabId next) noexcept;

}  // namespace kr
