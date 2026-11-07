#include <windows.h>

#include <array>
#include <stdexcept>

#include "krypton/testing/test.hpp"
#include "native_frame.hpp"
#include "native_input.hpp"

namespace kr::win {
namespace {
constexpr UINT kProbe = WM_APP + 100;
constexpr UINT kNested = WM_APP + 101;

struct Window {
  HWND handle = nullptr;
  explicit Window(HWND parent = nullptr, bool owned = false) {
    const auto style = parent && !owned ? WS_CHILD : WS_OVERLAPPEDWINDOW;
    handle = CreateWindowExW(0, L"STATIC", L"Krypton native test", style, 100,
                             100, 800, 600, parent, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    KR_ASSERT(handle != nullptr);
  }
  ~Window() {
    if (handle)
      DestroyWindow(handle);
  }
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
};

struct InputState {
  int probes = 0;
  int keys = 0;
  int focuses = 0;
  HWND focus = nullptr;
  bool consume = true;
  bool key_consumed = false;
  bool recurse = false;
  HWND nested = nullptr;
  NativeInput* input = nullptr;
  MSG original{};
};

bool Preprocess(void* context, const MSG& message) {
  auto& state = *static_cast<InputState*>(context);
  if (message.message == kProbe) {
    ++state.probes;
    return state.consume;
  }
  if (message.message == kNested) {
    PostMessageW(state.nested, kProbe, 0, 0);
    MSG nested{};
    KR_EXPECT(PeekMessageW(&nested, state.nested, kProbe, kProbe, PM_REMOVE));
    KR_EXPECT_EQ(nested.message, static_cast<UINT>(WM_NULL));
    return true;
  }
  if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
    state.key_consumed = state.input->ForwardUnhandledKey(
        static_cast<std::uint32_t>(message.wParam));
    KR_EXPECT(!state.input->ForwardUnhandledKey(
        static_cast<std::uint32_t>(message.wParam)));
    return state.key_consumed;
  }
  return false;
}

bool Unhandled(void* context, const MSG& message) {
  auto& state = *static_cast<InputState*>(context);
  ++state.keys;
  state.original = message;
  if (state.recurse)
    KR_EXPECT(!state.input->ForwardUnhandledKey(
        static_cast<std::uint32_t>(message.wParam)));
  return state.consume;
}

KR_TEST(NativeInputProcessesOnlyRemovedMessagesAndUnhooksOnReset) {
  Window window;
  InputState state;
  NativeInput input;
  KR_ASSERT(input.Initialize(window.handle, &state, Preprocess, Unhandled));
  PostMessageW(window.handle, kProbe, 42, 99);
  MSG message{};
  KR_ASSERT(PeekMessageW(&message, window.handle, kProbe, kProbe, PM_NOREMOVE));
  KR_EXPECT_EQ(message.message, kProbe);
  KR_EXPECT_EQ(state.probes, 0);
  KR_ASSERT(PeekMessageW(&message, window.handle, kProbe, kProbe, PM_REMOVE));
  KR_EXPECT_EQ(message.message, static_cast<UINT>(WM_NULL));
  KR_EXPECT_EQ(state.probes, 1);
  input.Reset();
  PostMessageW(window.handle, kProbe, 0, 0);
  KR_ASSERT(PeekMessageW(&message, window.handle, kProbe, kProbe, PM_REMOVE));
  KR_EXPECT_EQ(message.message, kProbe);
  KR_EXPECT_EQ(state.probes, 1);
}

KR_TEST(NativeInputSharesAThreadHookAcrossWindowsAndNestedLoops) {
  Window first;
  Window second(first.handle, true), unrelated;
  Window child(first.handle), owned(first.handle, true);
  InputState a, b;
  NativeInput one, two;
  KR_ASSERT(one.Initialize(first.handle, &a, Preprocess, Unhandled));
  KR_ASSERT(two.Initialize(second.handle, &b, Preprocess, Unhandled));
  KR_EXPECT(one.Owns(child.handle));
  KR_EXPECT(one.Owns(owned.handle));
  KR_EXPECT(!one.Owns(unrelated.handle));
  a.nested = second.handle;
  PostMessageW(first.handle, kNested, 0, 0);
  MSG message{};
  KR_ASSERT(PeekMessageW(&message, first.handle, kNested, kNested, PM_REMOVE));
  KR_EXPECT_EQ(b.probes, 1);
  KR_EXPECT_EQ(a.probes, 0);
  one.Reset();
  PostMessageW(second.handle, kProbe, 0, 0);
  KR_ASSERT(PeekMessageW(&message, second.handle, kProbe, kProbe, PM_REMOVE));
  KR_EXPECT_EQ(b.probes, 2);
  PostMessageW(unrelated.handle, kProbe, 0, 0);
  KR_ASSERT(
      PeekMessageW(&message, unrelated.handle, kProbe, kProbe, PM_REMOVE));
  KR_EXPECT_EQ(message.message, kProbe);
}

KR_TEST(NativeInputRecognizesOwnedInputIslandsWithoutMatchingWindowRoots) {
  Window browser;
  Window island(browser.handle, true);
  Window input_site(island.handle);
  Window popup(island.handle, true);
  Window unrelated;
  InputState state;
  NativeInput input;
  KR_ASSERT(input.Initialize(browser.handle, &state, Preprocess, Unhandled));
  KR_EXPECT(GetAncestor(input_site.handle, GA_ROOT) != browser.handle);
  KR_EXPECT(input.Owns(input_site.handle));
  KR_EXPECT(input.Owns(popup.handle));
  KR_EXPECT(!input.Owns(unrelated.handle));
  // Input from the owned island must reach the same shell as browser input.
  PostMessageW(input_site.handle, kProbe, 0, 0);
  MSG message{};
  KR_ASSERT(
      PeekMessageW(&message, input_site.handle, kProbe, kProbe, PM_REMOVE));
  KR_EXPECT_EQ(message.message, static_cast<UINT>(WM_NULL));
  KR_EXPECT_EQ(state.probes, 1);
}

KR_TEST(NativeInputPreservesOriginalKeysAndSkipsImePackets) {
  Window window;
  NativeInputTarget target;
  KR_ASSERT(target.Initialize(window.handle));
  InputState state;
  NativeInput input;
  state.input = &input;
  state.recurse = true;
  KR_ASSERT(input.Initialize(window.handle, &state, Preprocess, Unhandled));
  PostMessageW(window.handle, WM_SYSKEYDOWN, 'W', 0x20310001);
  MSG message{};
  KR_ASSERT(PeekMessageW(&message, window.handle, WM_SYSKEYDOWN, WM_SYSKEYDOWN,
                         PM_REMOVE));
  KR_EXPECT_EQ(state.keys, 1);
  KR_EXPECT(state.key_consumed);
  KR_EXPECT_EQ(state.original.message, static_cast<UINT>(WM_SYSKEYDOWN));
  KR_EXPECT_EQ(state.original.lParam, LPARAM{0x20310001});
  for (const auto key : {VK_PROCESSKEY, VK_PACKET}) {
    PostMessageW(window.handle, WM_KEYDOWN, key, 1);
    KR_ASSERT(PeekMessageW(&message, window.handle, WM_KEYDOWN, WM_KEYDOWN,
                           PM_REMOVE));
    KR_EXPECT(!state.key_consumed);
    KR_EXPECT_EQ(message.message, static_cast<UINT>(WM_KEYDOWN));
  }
  KR_EXPECT_EQ(state.keys, 1);
}

KR_TEST(NativeInputTargetsExcludeHostPagesAndBrowserOwnedPopups) {
  Window browser, other_browser;
  Window chrome(browser.handle), input_site(chrome.handle);
  Window page(browser.handle), extension_popup(browser.handle, true);
  Window popup(browser.handle, true), popup_input(popup.handle);
  Window nested_popup(popup.handle, true);
  NativeInputTarget target;
  KR_ASSERT(target.Initialize(chrome.handle));
  KR_EXPECT(NativeInputTarget::Contains(chrome.handle));
  KR_EXPECT(NativeInputTarget::Contains(input_site.handle));
  // Win32 normalizes the owner of a child-owned popup to the host. Register
  // native pop-out content itself rather than granting access to its owner.
  KR_EXPECT(!NativeInputTarget::Contains(popup_input.handle));
  NativeInputTarget popup_target;
  KR_ASSERT(popup_target.Initialize(popup.handle));
  KR_EXPECT(NativeInputTarget::Contains(popup_input.handle));
  KR_EXPECT(NativeInputTarget::Contains(nested_popup.handle));
  KR_EXPECT(!NativeInputTarget::Contains(browser.handle));
  KR_EXPECT(!NativeInputTarget::Contains(page.handle));
  KR_EXPECT(!NativeInputTarget::Contains(extension_popup.handle));
  KR_EXPECT(!NativeInputTarget::Contains(other_browser.handle));
  KR_EXPECT(!NativeInputTarget::Contains(nullptr));
  target.Reset();
  popup_target.Reset();
  KR_EXPECT(!NativeInputTarget::Contains(input_site.handle));
  KR_EXPECT(!NativeInputTarget::Contains(popup_input.handle));
}

KR_TEST(NativeInputTargetsValidateRegistrationAndReleaseItOnDestruction) {
  Window window;
  NativeInputTarget duplicate;
  KR_EXPECT(!duplicate.Initialize(nullptr));
  {
    NativeInputTarget target;
    KR_ASSERT(target.Initialize(window.handle));
    KR_EXPECT(!target.Initialize(window.handle));
    KR_EXPECT(!duplicate.Initialize(window.handle));
  }
  KR_EXPECT(!NativeInputTarget::Contains(window.handle));
  KR_ASSERT(duplicate.Initialize(window.handle));
}

KR_TEST(NativeInputDoesNotForwardPageKeysToChromeAccelerators) {
  Window browser;
  Window chrome(browser.handle), page(browser.handle);
  NativeInputTarget target;
  KR_ASSERT(target.Initialize(chrome.handle));
  InputState state;
  NativeInput input;
  state.input = &input;
  KR_ASSERT(input.Initialize(browser.handle, &state, Preprocess, Unhandled));
  constexpr LPARAM key_data = 0x001e0001;
  for (int repeat = 0; repeat != 3; ++repeat) {
    PostMessageW(page.handle, WM_KEYDOWN, 'A', key_data);
    MSG message{};
    KR_ASSERT(
        PeekMessageW(&message, page.handle, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE));
    KR_EXPECT_EQ(message.message, static_cast<UINT>(WM_KEYDOWN));
    KR_EXPECT_EQ(message.wParam, WPARAM{'A'});
    KR_EXPECT_EQ(message.lParam, key_data);
    KR_EXPECT(!state.key_consumed);
    KR_EXPECT_EQ(state.keys, repeat);

    PostMessageW(chrome.handle, WM_KEYDOWN, 'A', key_data);
    KR_ASSERT(PeekMessageW(&message, chrome.handle, WM_KEYDOWN, WM_KEYDOWN,
                           PM_REMOVE));
    KR_EXPECT_EQ(message.message, static_cast<UINT>(WM_NULL));
    KR_EXPECT(state.key_consumed);
    KR_EXPECT_EQ(state.keys, repeat + 1);
  }
}

KR_TEST(NativeInputContainsPreprocessorExceptions) {
  Window window;
  NativeInput input;
  KR_ASSERT(input.Initialize(
      window.handle, nullptr,
      [](void*, const MSG&) -> bool { throw std::runtime_error("test"); },
      nullptr));
  PostMessageW(window.handle, kProbe, 0, 0);
  MSG message{};
  KR_ASSERT(PeekMessageW(&message, window.handle, kProbe, kProbe, PM_REMOVE));
  KR_EXPECT_EQ(message.message, kProbe);
}

LPARAM AtClient(HWND window, int x, int y) {
  POINT point{x, y};
  ClientToScreen(window, &point);
  return MAKELPARAM(static_cast<short>(point.x), static_cast<short>(point.y));
}

KR_TEST(ReleaseFocusMovesKeysOffHiddenNativeContent) {
  Window host;
  Window chrome(host.handle), surface(host.handle), page(host.handle);
  Window surface_input(surface.handle);
  ShowWindow(host.handle, SW_SHOWNOACTIVATE);
  for (const HWND window :
       {chrome.handle, surface.handle, page.handle, surface_input.handle})
    ShowWindow(window, SW_SHOWNA);
  // Focus is this thread's input state; the test need not be foreground.
  SetFocus(surface_input.handle);
  KR_ASSERT(GetFocus() == surface_input.handle);
  ReleaseFocus(surface.handle, chrome.handle, host.handle);
  KR_EXPECT(GetFocus() == chrome.handle);
  // A fallback that has itself been hidden must not keep the keys either.
  SetFocus(surface_input.handle);
  ShowWindow(chrome.handle, SW_HIDE);
  ReleaseFocus(surface.handle, chrome.handle, host.handle);
  KR_EXPECT(GetFocus() == host.handle);
  // Focus outside the content stays where the host put it.
  SetFocus(page.handle);
  ReleaseFocus(surface.handle, chrome.handle, host.handle);
  KR_EXPECT(GetFocus() == page.handle);
  ShowWindow(host.handle, SW_HIDE);
}

KR_TEST(NativeInputObservesSentFocusChangesAndUnhooksOnReset) {
  Window host;
  Window chrome(host.handle), control(chrome.handle), page(host.handle);
  ShowWindow(host.handle, SW_SHOWNOACTIVATE);
  for (const HWND window : {chrome.handle, control.handle, page.handle})
    ShowWindow(window, SW_SHOWNA);
  SetFocus(page.handle);
  NativeInputTarget target;
  KR_ASSERT(target.Initialize(chrome.handle));
  InputState state;
  NativeInput input;
  KR_ASSERT(input.Initialize(host.handle, &state, Preprocess, nullptr,
                             [](void* context, HWND window) {
                               auto& state = *static_cast<InputState*>(context);
                               ++state.focuses;
                               state.focus = window;
                             }));
  // SetFocus sends its notifications; no queued message or XAML focus event
  // is needed for a late native focus grab to reach the shell.
  SetFocus(control.handle);
  KR_EXPECT(GetFocus() == control.handle);
  KR_EXPECT_EQ(state.focuses, 1);
  KR_EXPECT(state.focus == control.handle);
  SetFocus(page.handle);
  KR_EXPECT_EQ(state.focuses, 1);
  SetFocus(control.handle);
  KR_EXPECT_EQ(state.focuses, 2);
  input.Reset();
  SetFocus(page.handle);
  SetFocus(control.handle);
  KR_EXPECT_EQ(state.focuses, 2);
  ShowWindow(host.handle, SW_HIDE);
}

KR_TEST(NativeFrameUsesCachedDragGeometryAndPassesIslandHitsThrough) {
  Window parent, foreign;
  Window child(parent.handle), descendant(child.handle);
  NativeFrame frame;
  KR_ASSERT(frame.Initialize(parent.handle, nullptr, nullptr));
  KR_EXPECT(!frame.AttachIsland(foreign.handle));
  KR_ASSERT(frame.AttachIsland(child.handle));
  const std::array drag{Rect{0, 0, 500, 60}};
  const std::array controls{Rect{100, 0, 50, 60}};
  frame.Update(drag, controls, false);
  Window late_child(child.handle);
  const auto dpi = static_cast<float>(GetDpiForWindow(parent.handle)) /
                   USER_DEFAULT_SCREEN_DPI;
  const auto point = [&](float x, float y) {
    return AtClient(parent.handle, static_cast<int>(x * dpi),
                    static_cast<int>(y * dpi));
  };
  KR_EXPECT_EQ(SendMessageW(parent.handle, WM_NCHITTEST, 0, point(50, 30)),
               LRESULT{HTCAPTION});
  KR_EXPECT_EQ(SendMessageW(descendant.handle, WM_NCHITTEST, 0, point(50, 30)),
               LRESULT{HTTRANSPARENT});
  KR_EXPECT_EQ(SendMessageW(late_child.handle, WM_NCHITTEST, 0, point(50, 30)),
               LRESULT{HTTRANSPARENT});
  KR_EXPECT(SendMessageW(parent.handle, WM_NCHITTEST, 0, point(120, 30)) !=
            HTCAPTION);
  KR_EXPECT_EQ(SendMessageW(child.handle, WM_NCHITTEST, 0, point(1, 1)),
               LRESULT{HTTRANSPARENT});
  frame.Update(drag, controls, true);
  KR_EXPECT(SendMessageW(parent.handle, WM_NCHITTEST, 0, point(50, 30)) !=
            HTCAPTION);
  frame.Reset();
  // Destroying the windows afterwards must not access a dead frame object.
}

KR_TEST(NativeFrameRefreshesPositionAndHandlesChildDestruction) {
  Window parent;
  NativeFrame frame;
  KR_ASSERT(frame.Initialize(parent.handle, nullptr, nullptr));
  {
    Window child(parent.handle);
    KR_ASSERT(frame.AttachIsland(child.handle));
  }
  const std::array drag{Rect{0, 0, 500, 60}};
  frame.Update(drag, {}, false);
  SetWindowPos(parent.handle, nullptr, 300, 200, 800, 600,
               SWP_NOACTIVATE | SWP_NOZORDER);
  const auto dpi = static_cast<float>(GetDpiForWindow(parent.handle)) /
                   USER_DEFAULT_SCREEN_DPI;
  KR_EXPECT_EQ(SendMessageW(parent.handle, WM_NCHITTEST, 0,
                            AtClient(parent.handle, static_cast<int>(50 * dpi),
                                     static_cast<int>(30 * dpi))),
               LRESULT{HTCAPTION});
}

}  // namespace
}  // namespace kr::win
