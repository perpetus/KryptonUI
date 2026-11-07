#include "krypton/testing/test.hpp"
#include "krypton/ui/model/focus_ownership.hpp"

namespace kr {

KR_TEST(PointerFocusCanReturnButKeyboardAndTextKeepOwnership) {
  FocusOwnership focus;
  focus.Enter(FocusOwner::kPointerChrome);
  KR_EXPECT(focus.CanReturn(focus.generation()));
  focus.Enter(FocusOwner::kKeyboardChrome);
  KR_EXPECT(!focus.CanReturn(focus.generation()));
  focus.Enter(FocusOwner::kText);
  KR_EXPECT(!focus.CanReturn(focus.generation()));
}

KR_TEST(NewInteractionInvalidatesDeferredFocusReturnEvenForTheSameOwner) {
  FocusOwnership focus;
  focus.Enter(FocusOwner::kPointerChrome);
  const auto queued = focus.generation();
  focus.Enter(FocusOwner::kPointerChrome);
  KR_EXPECT(!focus.CanReturn(queued));
  KR_EXPECT(focus.CanReturn(focus.generation()));
}

KR_TEST(ClosingOneSurfaceDoesNotReleaseAnotherOrForgetKeyboardIntent) {
  FocusOwnership focus;
  focus.Enter(FocusOwner::kKeyboardChrome);
  focus.SetSurface(FocusSurface::kMenu, true);
  focus.SetSurface(FocusSurface::kFlyout, true);
  KR_EXPECT(focus.owner() == FocusOwner::kTransient);
  focus.SetSurface(FocusSurface::kMenu, false);
  KR_EXPECT(!focus.CanReturn(focus.generation()));
  focus.SetSurface(FocusSurface::kFlyout, false);
  KR_EXPECT(focus.owner() == FocusOwner::kKeyboardChrome);
  KR_EXPECT(!focus.CanReturn(focus.generation()));
}

KR_TEST(SurfaceUpdatesAreIdempotentAndClosingInvalidatesOldCallbacks) {
  FocusOwnership focus;
  focus.SetSurface(FocusSurface::kMenu, true);
  const auto opened = focus.generation();
  focus.SetSurface(FocusSurface::kMenu, true);
  KR_EXPECT_EQ(focus.generation(), opened);
  focus.SetSurface(FocusSurface::kMenu, false);
  KR_EXPECT(!focus.CanReturn(opened));
  KR_EXPECT(focus.CanReturn(focus.generation()));
}

KR_TEST(DeactivationInvalidatesFocusReturnAcrossReactivation) {
  FocusOwnership focus;
  focus.Enter(FocusOwner::kPointerChrome);
  const auto queued = focus.generation();
  focus.SetActive(false);
  KR_EXPECT(!focus.CanReturn(focus.generation()));
  focus.SetActive(true);
  KR_EXPECT(!focus.CanReturn(queued));
  KR_EXPECT(focus.CanReturn(focus.generation()));
}

}  // namespace kr
