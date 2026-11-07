#include "workspace_gesture.hpp"

#include <windows.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>

#include <limits>
#include <utility>

namespace kr::win {
namespace interactions = winrt::Microsoft::UI::Composition::Interactions;
using namespace interactions;

struct WorkspaceGestureOwner
    : winrt::implements<WorkspaceGestureOwner, IInteractionTrackerOwner> {
  WorkspaceGestureOwner(WorkspaceGesture& owner, std::weak_ptr<int> lifetime)
      : owner_(owner), lifetime_(std::move(lifetime)) {}
  void ValuesChanged(const InteractionTracker&,
                     const InteractionTrackerValuesChangedArgs& args) {
    if (!lifetime_.expired())
      owner_.Update(args.Position().x);
  }
  void InteractingStateEntered(
      const InteractionTracker&,
      const InteractionTrackerInteractingStateEnteredArgs&) {
    if (!lifetime_.expired())
      owner_.Begin();
  }
  void IdleStateEntered(const InteractionTracker&,
                        const InteractionTrackerIdleStateEnteredArgs&) {
    if (!lifetime_.expired())
      owner_.End();
  }
  void InertiaStateEntered(const InteractionTracker&,
                           const InteractionTrackerInertiaStateEnteredArgs&) {}
  void CustomAnimationStateEntered(
      const InteractionTracker&,
      const InteractionTrackerCustomAnimationStateEnteredArgs&) {}
  void RequestIgnored(const InteractionTracker&,
                      const InteractionTrackerRequestIgnoredArgs&) {}

 private:
  WorkspaceGesture& owner_;
  std::weak_ptr<int> lifetime_;
};

WorkspaceGesture::WorkspaceGesture(ViewContext& context,
                                   const xaml::UIElement& root)
    : context_(context), root_(root) {}

WorkspaceGesture::~WorkspaceGesture() {
  lifetime_.reset();
  try {
    if (tracker_)
      tracker_.InteractionSources().RemoveAll();
  } catch (...) {
    OutputDebugStringW(L"KryptonUI gesture source could not detach\n");
  }
}

void WorkspaceGesture::Configure(bool enabled) {
  if (enabled_ == enabled)
    return;
  enabled_ = enabled;
  if (!enabled) {
    Cancel();
  }
  if (enabled && !tracker_) {
    const auto visual =
        xaml::Hosting::ElementCompositionPreview::GetElementVisual(root_);
    source_ = VisualInteractionSource::Create(visual);
    source_.ManipulationRedirectionMode(
        VisualInteractionSourceRedirectionMode::CapableTouchpadOnly);
    source_.IsPositionXRailsEnabled(true);
    source_.PositionYSourceMode(InteractionSourceMode::Disabled);
    source_.ScaleSourceMode(InteractionSourceMode::Disabled);
    source_.PositionXChainingMode(InteractionChainingMode::Never);
    source_.PositionYChainingMode(InteractionChainingMode::Auto);
    tracker_ = InteractionTracker::CreateWithOwner(
        visual.Compositor(),
        winrt::make<WorkspaceGestureOwner>(*this, lifetime_));
    const auto extent = std::numeric_limits<float>::infinity();
    tracker_.MinPosition({-extent, 0, 0});
    tracker_.MaxPosition({extent, 0, 0});
    tracker_.InteractionSources().Add(source_);
  }
  if (source_)
    source_.PositionXSourceMode(enabled
                                    ? InteractionSourceMode::EnabledWithInertia
                                    : InteractionSourceMode::Disabled);
}

bool WorkspaceGesture::Allowed() const {
  return enabled_ && root_.Visibility() == xaml::Visibility::Visible &&
         GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_SHIFT) >= 0 &&
         GetKeyState(VK_MENU) >= 0 && GetKeyState(VK_LBUTTON) >= 0 &&
         GetKeyState(VK_RBUTTON) >= 0 && GetKeyState(VK_MBUTTON) >= 0 &&
         GetKeyState(VK_XBUTTON1) >= 0 && GetKeyState(VK_XBUTTON2) >= 0 &&
         (!context_.workspace_gestures_allowed ||
          context_.workspace_gestures_allowed());
}

void WorkspaceGesture::Begin() {
  Cancel();
  if (!Allowed() || !context_.pointer_over(root_.as<xaml::FrameworkElement>()))
    return;
  tracking_ = true;
  origin_ = tracker_.Position().x;
  if (context_.workspace_gesture_event)
    context_.workspace_gesture_event(WorkspaceSwipePhase::kBegin);
  (void)context_.delegate.OnWorkspaceSwipe({WorkspaceSwipePhase::kBegin});
}

void WorkspaceGesture::Update(float position) {
  if (!tracking_)
    return;
  if (!Allowed()) {
    Cancel();
    return;
  }
  if (context_.workspace_gesture_event)
    context_.workspace_gesture_event(WorkspaceSwipePhase::kUpdate);
  (void)context_.delegate.OnWorkspaceSwipe(
      {WorkspaceSwipePhase::kUpdate, position - origin_});
}

void WorkspaceGesture::End() {
  if (std::exchange(tracking_, false)) {
    if (context_.workspace_gesture_event)
      context_.workspace_gesture_event(WorkspaceSwipePhase::kEnd);
    (void)context_.delegate.OnWorkspaceSwipe({WorkspaceSwipePhase::kEnd});
  }
}

void WorkspaceGesture::Cancel() {
  if (std::exchange(tracking_, false)) {
    if (context_.workspace_gesture_event)
      context_.workspace_gesture_event(WorkspaceSwipePhase::kCancel);
    (void)context_.delegate.OnWorkspaceSwipe({WorkspaceSwipePhase::kCancel});
  }
}

}  // namespace kr::win
