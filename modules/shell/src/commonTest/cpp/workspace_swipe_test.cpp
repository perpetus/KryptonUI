#include <limits>

#include "krypton/testing/test.hpp"
#include "krypton/ui/model/workspace_swipe.hpp"

namespace kr {

KR_TEST(WorkspaceSwipeRequiresAnExplicitTouchpadGestureAndThreshold) {
  WorkspaceSwipeState swipe;
  KR_EXPECT(!swipe.Update({WorkspaceSwipePhase::kUpdate, 100}).direction);
  (void)swipe.Update({WorkspaceSwipePhase::kBegin});
  KR_EXPECT(!swipe.Update({WorkspaceSwipePhase::kUpdate, 47}).direction);
  KR_EXPECT(swipe.Update({WorkspaceSwipePhase::kUpdate, 48}).direction ==
            WorkspaceSwipeDirection::kNext);
}

KR_TEST(WorkspaceSwipeSwitchesOnceIncludingMomentumAndDirectionReversal) {
  WorkspaceSwipeState swipe;
  (void)swipe.Update({WorkspaceSwipePhase::kBegin});
  KR_EXPECT(swipe.Update({WorkspaceSwipePhase::kUpdate, 80}).direction.has_value());
  KR_EXPECT(!swipe.Update({WorkspaceSwipePhase::kUpdate, 500}).direction);
  KR_EXPECT(!swipe.Update({WorkspaceSwipePhase::kUpdate, -500}).direction);
  (void)swipe.Update({WorkspaceSwipePhase::kEnd});
  KR_EXPECT(!swipe.Update({WorkspaceSwipePhase::kUpdate, 100}).direction);
  (void)swipe.Update({WorkspaceSwipePhase::kBegin});
  KR_EXPECT(swipe.Update({WorkspaceSwipePhase::kUpdate, -48}).direction ==
            WorkspaceSwipeDirection::kPrevious);
}

KR_TEST(VerticalWorkspaceGestureNeverBecomesAHorizontalSwitch) {
  WorkspaceSwipeState swipe;
  (void)swipe.Update({WorkspaceSwipePhase::kBegin});
  KR_EXPECT(!swipe.Update({WorkspaceSwipePhase::kUpdate, 4, 30}).consume);
  KR_EXPECT(!swipe.Update({WorkspaceSwipePhase::kUpdate, 90, 35}).direction);
}

KR_TEST(WorkspaceSwipeCancellationAndInvalidNumbersDiscardTheGesture) {
  WorkspaceSwipeState swipe;
  (void)swipe.Update({WorkspaceSwipePhase::kBegin});
  (void)swipe.Update({WorkspaceSwipePhase::kUpdate, 40});
  (void)swipe.Update({WorkspaceSwipePhase::kCancel});
  KR_EXPECT(!swipe.Update({WorkspaceSwipePhase::kUpdate, 60}).direction);
  (void)swipe.Update({WorkspaceSwipePhase::kBegin});
  KR_EXPECT(!swipe.Update({WorkspaceSwipePhase::kUpdate,
                          std::numeric_limits<float>::quiet_NaN()}).consume);
  KR_EXPECT(!swipe.Update({WorkspaceSwipePhase::kUpdate, 90}).direction);
}

}  // namespace kr
