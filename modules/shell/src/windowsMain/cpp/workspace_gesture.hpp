#pragma once

#include <winrt/Microsoft.UI.Composition.Interactions.h>

#include <memory>

#include "view_context.hpp"

namespace kr::win {

struct WorkspaceGestureOwner;

// A passive compositor tracker: it observes horizontal touchpad movement and
// never translates the sidebar or redirects mouse/touch/pen input.
class WorkspaceGesture {
 public:
  WorkspaceGesture(ViewContext& context, const xaml::UIElement& root);
  ~WorkspaceGesture();
  void Configure(bool enabled);
  void Cancel();

 private:
  friend struct WorkspaceGestureOwner;
  void Begin();
  void Update(float position);
  void End();
  [[nodiscard]] bool Allowed() const;
  ViewContext& context_;
  xaml::UIElement root_{nullptr};
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  winrt::Microsoft::UI::Composition::Interactions::InteractionTracker tracker_{
      nullptr};
  winrt::Microsoft::UI::Composition::Interactions::VisualInteractionSource
      source_{nullptr};
  float origin_ = 0;
  bool enabled_ = false;
  bool tracking_ = false;
};

}  // namespace kr::win
