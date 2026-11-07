#include "krypton/ui/layout/frame_hit_test.hpp"

#include <array>

#include "krypton/testing/test.hpp"
#include "krypton/ui/layout/drag_regions.hpp"
#include "krypton/ui/presentation/presenter.hpp"
#include "krypton/ui/state/store.hpp"

namespace kr {
namespace {
KR_TEST(FrameHitTestKeepsAllEightResizeEdgesAheadOfDragRegions) {
  const FrameGeometry geometry{{0, 0, 800, 600}, 8, 6, true};
  const std::array drag{Rect{0, 0, 800, 40}};
  const std::array points{Point{1, 1},     Point{400, 1},   Point{799, 1},
                          Point{1, 300},   Point{799, 300}, Point{1, 599},
                          Point{400, 599}, Point{799, 599}};
  const std::array expected{FrameHit::kTopLeft,  FrameHit::kTop,
                            FrameHit::kTopRight, FrameHit::kLeft,
                            FrameHit::kRight,    FrameHit::kBottomLeft,
                            FrameHit::kBottom,   FrameHit::kBottomRight};
  for (std::size_t i = 0; i < points.size(); ++i)
    KR_EXPECT(HitTestFrame(geometry, points[i], drag) == expected[i]);
  KR_EXPECT(HitTestFrame(geometry, {400, 20}, drag) == FrameHit::kCaption);
  KR_EXPECT(HitTestFrame(geometry, {400, 300}, drag) == FrameHit::kClient);
}

KR_TEST(FrameHitTestUsesMeasuredControlExclusionsAtFractionalCoordinates) {
  DragRegions regions;
  const std::array drag{Rect{0, 0, 800, 40}};
  const std::array controls{Rect{100.25f, 4.5f, 80.5f, 30.25f}};
  const auto remaining = regions.Update(drag, controls);
  const FrameGeometry geometry{{0, 0, 800, 600}};
  KR_EXPECT(HitTestFrame(geometry, {100.25f, 10}, remaining) ==
            FrameHit::kClient);
  KR_EXPECT(HitTestFrame(geometry, {180.74f, 10}, remaining) ==
            FrameHit::kClient);
  KR_EXPECT(HitTestFrame(geometry, {180.75f, 10}, remaining) ==
            FrameHit::kCaption);
}

KR_TEST(FrameHitTestHandlesMaximizedFullscreenAndOutsideClientPoints) {
  FrameGeometry geometry{{0, 0, 800, 600}, 8, 8, false};
  const std::array drag{Rect{0, 0, 800, 40}};
  KR_EXPECT(HitTestFrame(geometry, {1, 1}, drag) == FrameHit::kCaption);
  KR_EXPECT(HitTestFrame(geometry, {-1, 20}, drag) == FrameHit::kClient);
  KR_EXPECT(HitTestFrame(geometry, {800, 20}, drag) == FrameHit::kClient);
  geometry.fullscreen = true;
  geometry.resizable = true;
  KR_EXPECT(HitTestFrame(geometry, {1, 1}, drag) == FrameHit::kClient);
}

KR_TEST(PresenterReportsBothKindsOfFullscreenToTheNativeFrame) {
  Store store;
  Presenter presenter;
  const ShellLayout layout;
  for (const auto flag :
       {WindowFlag::kFullscreen, WindowFlag::kTabFullscreen}) {
    store.SetWindow({.flags = flag});
    KR_EXPECT(
        presenter.Present(store, layout, store.TakeDirty()).header.fullscreen);
  }
  store.SetWindow({});
  KR_EXPECT(
      !presenter.Present(store, layout, store.TakeDirty()).header.fullscreen);
}
}  // namespace
}  // namespace kr
