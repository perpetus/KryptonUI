#include "sidebar_splitter.hpp"

#include <windows.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

namespace kr::win {

SidebarSplitter::SidebarSplitter(ViewContext& context) : context_(context) {
  root_.Width(kTokens.spacing.splitter_width);
  root_.Background(context_.brushes.transparent);
  root_.as<xaml::IUIElementProtected>().ProtectedCursor(
      winrt::Microsoft::UI::Input::InputSystemCursor::Create(
          winrt::Microsoft::UI::Input::InputSystemCursorShape::SizeWestEast));
  Accessible(root_, NativeText(UiText::kResizeSidebar));
  SetToolTip(root_, NativeText(UiText::kResizeHint));
  pressed_ = root_.PointerPressed(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (!args.GetCurrentPoint(root_).Properties().IsLeftButtonPressed())
          return;
        POINT cursor;
        if (!GetCursorPos(&cursor) || !root_.CapturePointer(args.Pointer()))
          return;
        dragging_ = true;
        right_ = layout_.sidebar_on_right;
        start_x_ = cursor.x;
        start_width_ = layout_.sidebar_width;
        args.Handled(true);
      });
  moved_ = root_.PointerMoved(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (!dragging_)
          return;
        POINT cursor;
        if (GetCursorPos(&cursor)) {
          const float delta = static_cast<float>(cursor.x - start_x_) / scale_;
          context_.delegate.OnSidebarResized(
              SidebarWidthAfterDrag(start_width_, delta, right_));
        }
        args.Handled(true);
      });
  released_ = root_.PointerReleased(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (dragging_) {
          Finish();
          args.Handled(true);
        }
      });
  capture_lost_ = root_.PointerCaptureLost(
      winrt::auto_revoke, [this](auto&&, auto&&) { dragging_ = false; });
  double_tapped_ = root_.DoubleTapped(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::DoubleTappedRoutedEventArgs& args) {
        Finish();
        context_.delegate.OnSidebarToggled(SidebarToggle::kCollapse);
        args.Handled(true);
      });
}

void SidebarSplitter::SetText(UiTextView text) {
  Accessible(root_, NativeText(UiText::kResizeSidebar, text));
  SetToolTip(root_, NativeText(UiText::kResizeHint, text));
}

void SidebarSplitter::Finish() {
  dragging_ = false;
  root_.ReleasePointerCaptures();
}

void SidebarSplitter::SetLayout(const ShellLayout& layout, float scale) {
  layout_ = layout;
  scale_ = scale;
  root_.HorizontalAlignment(layout.sidebar_on_right
                                ? xaml::HorizontalAlignment::Left
                                : xaml::HorizontalAlignment::Right);
  root_.Visibility(layout.sidebar_visible && !layout.sidebar_icon_only &&
                           !layout.sidebar_overlays
                       ? xaml::Visibility::Visible
                       : xaml::Visibility::Collapsed);
}

}  // namespace kr::win
