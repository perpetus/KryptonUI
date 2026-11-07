#include "input_smoke.hpp"

#include <cstdio>

#include "krypton/ui/design/tokens.generated.hpp"

namespace kr::preview {

bool InputSmoke::Observe(const MSG& message) noexcept {
  if (message.message != WM_KEYDOWN || message.wParam != VK_F6)
    return false;
  ++keys_;
  if (keys_ != 1 || message.hwnd != target_ || message.lParam != key_data_)
    failure_ = "Input smoke: duplicate or altered native key fallback";
  return true;
}

void InputSmoke::ObservePage(UINT message,
                             WPARAM wparam,
                             LPARAM lparam) noexcept {
  if (phase_ != Phase::kWaitPageInput)
    return;
  if ((message == WM_KEYDOWN || message == WM_KEYUP) && wparam == VK_F8) {
    const auto expected =
        message == WM_KEYDOWN ? LPARAM{0x00420001} : LPARAM{0xc0420001};
    auto& count = message == WM_KEYDOWN ? page_keys_ : page_releases_;
    if (++count > 3 || lparam != expected)
      failure_ =
          "Input smoke: page key duplicated or changed after chrome focus";
  } else if (message == WM_CHAR && wparam == L'\u03bb') {
    if (++page_characters_ > 3 || lparam != LPARAM{0x00420001})
      failure_ = "Input smoke: page character duplicated or changed";
  }
}

bool InputSmoke::Advance(unsigned frame,
                         HWND parent,
                         fixtures::MockBrowser& browser) {
  if (failure_)
    return false;
  if (frame >= 600 && !Complete()) {
    switch (phase_) {
      case Phase::kOpenWorkspace:
      case Phase::kWaitWorkspace:
        failure_ = "Input smoke: workspace editor did not acquire focus";
        break;
      case Phase::kOpenSearch:
      case Phase::kWaitFocus:
        failure_ =
            "Input smoke: search input did not acquire native child focus";
        break;
      case Phase::kWaitKey:
        failure_ = "Input smoke: search key did not reach host fallback";
        break;
      case Phase::kWaitSelection:
        failure_ =
            "Input smoke: native suggestion selection did not reach the host";
        std::fprintf(
            stderr, "Selection state: index %lld, flags %u, request %u\n",
            browser.selected_suggestion()
                ? static_cast<long long>(*browser.selected_suggestion())
                : -1LL,
            browser.address_flags(), browser.address_state().request_id);
        break;
      case Phase::kWaitToolbarFocus:
        failure_ =
            "Input smoke: semantic toolbar focus did not leave address editing";
        break;
      case Phase::kWaitPagePress:
        failure_ =
            "Input smoke: a page press did not take keyboard focus from chrome";
        break;
      case Phase::kWaitPageFocusReturn:
        failure_ = "Input smoke: a late native focus grab stranded page keys";
        break;
      case Phase::kWaitPageMenu:
        failure_ = "Input smoke: the page menu did not take keyboard focus";
        break;
      case Phase::kWaitPageMenuClosed:
        failure_ = "Input smoke: closing the page menu left keys with chrome";
        break;
      case Phase::kWaitPageInput:
        failure_ = "Input smoke: page input was swallowed after chrome focus";
        break;
      case Phase::kWaitFullscreen:
        failure_ = "Input smoke: fullscreen command did not complete";
        break;
      case Phase::kWaitWindowed:
        failure_ = "Input smoke: leaving fullscreen did not complete";
        break;
      case Phase::kWaitMaximized:
        failure_ = "Input smoke: maximize did not complete";
        break;
      case Phase::kWaitRestored:
        failure_ = "Input smoke: restore did not complete";
        break;
      case Phase::kDone:
        break;
    }
    return false;
  }
  switch (phase_) {
    case Phase::kOpenWorkspace:
      ShowWindow(parent, SW_SHOW);
      SetActiveWindow(parent);
      SetFocus(parent);
      browser.ShowWorkspaceEditor(2);
      ready_after_ = GetTickCount64() +
                     static_cast<ULONGLONG>(design::kTokens.motion.slow_ms * 3);
      phase_ = Phase::kWaitWorkspace;
      break;
    case Phase::kWaitWorkspace: {
      const auto focus = GetFocus();
      if (GetTickCount64() >= ready_after_ && focus && focus != parent &&
          (IsChild(parent, focus) ||
           GetAncestor(focus, GA_ROOTOWNER) == parent))
        phase_ = Phase::kOpenSearch;
      break;
    }
    case Phase::kOpenSearch:
      // Start-Process uses a hidden startup flag for bounded runs; a second
      // ShowWindow explicitly makes this preview eligible for keyboard focus.
      ShowWindow(parent, SW_SHOW);
      SetActiveWindow(parent);
      SetFocus(parent);
      browser.FocusAddress(true);
      ready_after_ = GetTickCount64() +
                     static_cast<ULONGLONG>(design::kTokens.motion.slow_ms * 3);
      phase_ = Phase::kWaitFocus;
      break;
    case Phase::kWaitFocus:
      target_ = GetFocus();
      if (GetTickCount64() >= ready_after_ && target_ &&
          (browser.address_flags() & KR_ADDRESS_FOCUSED) != 0 &&
          IsChild(parent, target_)) {
        if (!PostMessageW(target_, WM_KEYDOWN, VK_DOWN, 1)) {
          failure_ = "Input smoke: could not post suggestion selection";
          return false;
        }
        phase_ = Phase::kWaitSelection;
      }
      break;
    case Phase::kWaitSelection:
      if (browser.selected_suggestion() == std::optional<std::size_t>{1}) {
        PostMessageW(target_, WM_KEYUP, VK_DOWN, LPARAM{0xc0000001});
        key_data_ =
            1 | static_cast<LPARAM>(MapVirtualKeyW(VK_F6, MAPVK_VK_TO_VSC))
                    << 16;
        if (!PostMessageW(target_, WM_KEYDOWN, VK_F6, key_data_)) {
          failure_ = "Input smoke: could not post a native key";
          return false;
        }
        phase_ = Phase::kWaitKey;
      }
      break;
    case Phase::kWaitKey:
      if (keys_ == 1) {
        PostMessageW(target_, WM_KEYUP, VK_F6, key_data_ | LPARAM{0xc0000000});
        if (!browser.FocusNative(KR_ELEMENT_APP_MENU)) {
          failure_ = "Input smoke: semantic toolbar focus was rejected";
          return false;
        }
        phase_ = Phase::kWaitToolbarFocus;
      }
      break;
    case Phase::kWaitToolbarFocus:
      if ((browser.address_flags() & (KR_ADDRESS_OPEN | KR_ADDRESS_FOCUSED)) ==
              0 &&
          GetFocus() && GetFocus() != target_) {
        if (!IsChild(parent, GetFocus())) {
          failure_ = "Input smoke: semantic toolbar focus is not in chrome";
          return false;
        }
        chrome_target_ = GetFocus();
        // Chrome holds keyboard focus. A press on the page must take it back
        // before the host sees the press, as Win32 never does so for a child.
        const auto content = browser.content_bounds();
        const float scale = static_cast<float>(GetDpiForWindow(parent)) /
                            USER_DEFAULT_SCREEN_DPI;
        const auto x =
            static_cast<int>((content.x + content.width / 2) * scale);
        const auto y =
            static_cast<int>((content.y + content.height / 2) * scale);
        if (!PostMessageW(parent, WM_RBUTTONDOWN, MK_RBUTTON,
                          MAKELPARAM(x, y))) {
          failure_ = "Input smoke: could not post a page press";
          return false;
        }
        phase_ = Phase::kWaitPagePress;
      }
      break;
    case Phase::kWaitPagePress:
      if (GetFocus() == parent) {
        // XAML can keep the same logical focus while taking Windows focus
        // again after a page press. No second XAML GotFocus is raised.
        SetFocus(chrome_target_);
        if (GetFocus() == parent) {
          failure_ = "Input smoke: could not reproduce a late focus grab";
          return false;
        }
        phase_ = Phase::kWaitPageFocusReturn;
      }
      break;
    case Phase::kWaitPageFocusReturn:
      if (GetFocus() == parent) {
        // The keyboard form of a page context menu, opened over the page.
        PostMessageW(parent, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(parent),
                     -1);
        phase_ = Phase::kWaitPageMenu;
      }
      break;
    case Phase::kWaitPageMenu:
      if (const HWND focus = GetFocus();
          focus && focus != parent &&
          (IsChild(parent, focus) ||
           GetAncestor(focus, GA_ROOTOWNER) == parent)) {
        PostMessageW(focus, WM_KEYDOWN, VK_ESCAPE, 1);
        ready_after_ =
            GetTickCount64() +
            static_cast<ULONGLONG>(design::kTokens.motion.slow_ms * 3);
        phase_ = Phase::kWaitPageMenuClosed;
      }
      break;
    case Phase::kWaitPageMenuClosed:
      // Wait out the close, so a late XAML focus restore would be seen too.
      if (GetTickCount64() >= ready_after_ && GetFocus() == parent) {
        phase_ = Phase::kWaitPageInput;
        // F8 produces no translated character; send a separate Unicode
        // character so both key and text routing cross the real input hook.
        for (unsigned repeat = 0; repeat != 3; ++repeat) {
          if (!PostMessageW(parent, WM_KEYDOWN, VK_F8, LPARAM{0x00420001}) ||
              !PostMessageW(parent, WM_CHAR, L'\u03bb', LPARAM{0x00420001}) ||
              !PostMessageW(parent, WM_KEYUP, VK_F8, LPARAM{0xc0420001})) {
            failure_ = "Input smoke: could not post page input";
            return false;
          }
        }
      }
      break;
    case Phase::kWaitPageInput:
      if (page_keys_ == 3 && page_releases_ == 3 && page_characters_ == 3) {
        PostMessageW(parent, WM_KEYDOWN, VK_F11, 1);
        phase_ = Phase::kWaitFullscreen;
      }
      break;
    case Phase::kWaitFullscreen:
      if (browser.fullscreen()) {
        PostMessageW(parent, WM_KEYDOWN, VK_F11, 1);
        phase_ = Phase::kWaitWindowed;
      }
      break;
    case Phase::kWaitWindowed:
      if (!browser.fullscreen()) {
        PostMessageW(parent, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
        phase_ = Phase::kWaitMaximized;
      }
      break;
    case Phase::kWaitMaximized:
      if (IsZoomed(parent)) {
        PostMessageW(parent, WM_SYSCOMMAND, SC_RESTORE, 0);
        phase_ = Phase::kWaitRestored;
      }
      break;
    case Phase::kWaitRestored:
      if (!IsZoomed(parent)) {
        // Keep an asynchronous native dialog alive until the host destroys
        // the shell, exercising its cancellation and runtime shutdown order.
        browser.ShowWorkspaceEditor(2);
        phase_ = Phase::kDone;
      }
      break;
    case Phase::kDone:
      break;
  }
  return true;
}

}  // namespace kr::preview
