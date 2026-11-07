#include <algorithm>
#include <cmath>

#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/model/workspace_swipe.hpp"

namespace kr {

WorkspaceSwipeResult WorkspaceSwipeState::Update(
    WorkspaceSwipeInput input) noexcept {
  if (input.phase == WorkspaceSwipePhase::kEnd ||
      input.phase == WorkspaceSwipePhase::kCancel) {
    Reset();
    return {};
  }
  if (input.phase == WorkspaceSwipePhase::kBegin) {
    Reset();
    tracking_ = true;
    return {};
  }
  if (!tracking_ || !std::isfinite(input.horizontal) ||
      !std::isfinite(input.vertical)) {
    Reset();
    return {};
  }
  if (axis_ == Axis::kVertical)
    return {};
  if (switched_)
    return {.consume = true};
  const auto& token = design::kTokens.workspace_swipe;
  const float horizontal = std::abs(input.horizontal);
  const float vertical = std::abs(input.vertical);
  if (std::max(horizontal, vertical) >= token.axis_lock_delta) {
    if (vertical >= horizontal * token.axis_dominance)
      axis_ = Axis::kVertical;
    else if (horizontal >= vertical * token.axis_dominance)
      axis_ = Axis::kHorizontal;
  }
  if (axis_ == Axis::kVertical)
    return {};
  if (axis_ == Axis::kHorizontal && horizontal >= token.activation_delta) {
    switched_ = true;
    return {.consume = true,
            .direction = input.horizontal > 0
                             ? WorkspaceSwipeDirection::kNext
                             : WorkspaceSwipeDirection::kPrevious};
  }
  return {.consume = horizontal != 0};
}

}  // namespace kr
