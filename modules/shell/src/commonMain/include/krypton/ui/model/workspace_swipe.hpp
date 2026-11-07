#pragma once

#include <optional>

namespace kr {

enum class WorkspaceSwipeDirection { kPrevious, kNext };
enum class WorkspaceSwipePhase { kBegin, kUpdate, kEnd, kCancel };

struct WorkspaceSwipeInput {
  WorkspaceSwipePhase phase = WorkspaceSwipePhase::kUpdate;
  float horizontal = 0;  // DIPs relative to the beginning of the gesture.
  float vertical = 0;
};
struct WorkspaceSwipeResult {
  bool consume = false;
  std::optional<WorkspaceSwipeDirection> direction;
};

// A gesture includes inertia; only completion releases the one-switch latch.
class WorkspaceSwipeState {
 public:
  [[nodiscard]] WorkspaceSwipeResult Update(WorkspaceSwipeInput input) noexcept;
  void Reset() noexcept { *this = {}; }

 private:
  enum class Axis { kUndecided, kHorizontal, kVertical };
  Axis axis_ = Axis::kUndecided;
  bool tracking_ = false;
  bool switched_ = false;
};

}  // namespace kr
