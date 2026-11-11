#pragma once

#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.Foundation.Collections.h>

#include "glass_material.hpp"
#include "native_images.hpp"
#include "native_motion.hpp"
#include "native_theme.hpp"
#include "xaml_factory.hpp"

namespace kr::win {

class TabPreviewSurface;
class NativeInput;
class IconSet;

namespace media = xaml::Media;
namespace foundation = winrt::Windows::Foundation;
using foundation::IInspectable;

// Synchronous services used by native controls. Borrowed for their lifetime.
struct ViewContext {
  ShellViewDelegate& delegate;
  Brushes& brushes;
  ImageCache& images;
  std::move_only_function<Rect(const xaml::FrameworkElement&)> bounds;
  // Whether the mouse cursor is over the element now, in this window. Pointer
  // events report the last position inside an island when it is left for
  // another window, so they cannot answer this alone.
  std::move_only_function<bool(const xaml::FrameworkElement&)> pointer_over;
  std::move_only_function<void(MenuKind, std::uint64_t, Rect)> request_menu;
  std::move_only_function<void()> hover_changed;
  TabPreviewSurface& tab_previews;
  NativeMotion& motion;
  NativeInput& input;
  IconSet& icons;  // the chrome's swappable icon pack
  GlassMaterials& glass;
  // Includes native popups, which do not necessarily appear in shared state.
  std::move_only_function<bool()> workspace_gestures_allowed;
  std::move_only_function<void(WorkspaceSwipePhase)> workspace_gesture_event;
  // Ignore this preview's own flyout, but yield to other interactive surfaces.
  // An active preview keeps its own press/capture until that interaction ends.
  std::move_only_function<bool(const xaml::FrameworkElement&, bool)>
      hover_popup_allowed;
  // Finish another hover preview before an explicit surface takes focus.
  std::move_only_function<void(const xaml::FrameworkElement&)>
      dismiss_hover_popups;
  std::move_only_function<void(Point, bool)> url_drag_changed;
};

}  // namespace kr::win
