#include "native_input.hpp"

#include <commctrl.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace kr::win {

// DesktopWindowXamlSource registers for its parent's caption input even when
// its own island is hidden. Sharing the browser's parent lets each island
// replay an old caption point into the thread's shared pointer-enter state.
// This host has no non-client region, draws nothing and passes empty client
// space through. Retain one per browser; XAML controls and sources stay intact.
class IslandInputHost {
 public:
  explicit IslandInputHost(HWND parent) : parent_(parent) {}
  [[nodiscard]] Result<void> Initialize() {
    static const bool registered = [] {
      WNDCLASSEXW info{sizeof(info)};
      info.lpfnWndProc = WindowProc;
      info.hInstance = GetModuleHandleW(nullptr);
      info.lpszClassName = L"KryptonUI.IslandInputHost";
      return RegisterClassExW(&info) != 0 ||
             GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    if (!registered)
      return Fail(ErrorCode::kPlatform, "Could not register island input host");
    RECT bounds{};
    if (!GetClientRect(parent_, &bounds))
      return Fail(ErrorCode::kPlatform,
                  "Could not read island input host bounds");
    window_ = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP, L"KryptonUI.IslandInputHost", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
        bounds.right, bounds.bottom, parent_, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!window_)
      return Fail(ErrorCode::kPlatform, "Could not create island input host");
    if (!SetWindowSubclass(parent_, ParentProc,
                           reinterpret_cast<UINT_PTR>(this),
                           reinterpret_cast<DWORD_PTR>(this))) {
      return Fail(ErrorCode::kPlatform, "Could not attach island input host");
    }
    size_ = {bounds.right, bounds.bottom};
    return {};
  }
  ~IslandInputHost() {
    if (parent_)
      RemoveWindowSubclass(parent_, ParentProc,
                           reinterpret_cast<UINT_PTR>(this));
    if (window_)
      DestroyWindow(window_);
  }
  IslandInputHost(const IslandInputHost&) = delete;
  IslandInputHost& operator=(const IslandInputHost&) = delete;

  [[nodiscard]] HWND Window() const noexcept { return window_; }
  void Raise() const noexcept {
    if (window_ && GetWindow(window_, GW_HWNDPREV))
      SetWindowPos(window_, HWND_TOP, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  [[nodiscard]] static Result<std::shared_ptr<IslandInputHost>> Acquire(
      HWND parent) {
    static thread_local std::vector<std::weak_ptr<IslandInputHost>> hosts;
    std::erase_if(hosts, [](const auto& host) { return host.expired(); });
    for (const auto& host : hosts)
      if (auto value = host.lock(); value && value->parent_ == parent)
        return value;
    auto host = std::make_shared<IslandInputHost>(parent);
    if (auto result = host->Initialize(); !result)
      return std::unexpected(result.error());
    hosts.push_back(host);
    return host;
  }

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam) noexcept {
    if (message == WM_NCHITTEST)
      return HTTRANSPARENT;
    return DefWindowProcW(window, message, wparam, lparam);
  }
  static LRESULT CALLBACK ParentProc(HWND parent,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam,
                                     UINT_PTR id,
                                     DWORD_PTR context) noexcept {
    auto& self = *reinterpret_cast<IslandInputHost*>(context);
    if (message == WM_SIZE && self.window_) {
      RECT bounds{};
      if (GetClientRect(parent, &bounds) &&
          (self.size_.cx != bounds.right || self.size_.cy != bounds.bottom)) {
        self.size_ = {bounds.right, bounds.bottom};
        SetWindowPos(self.window_, nullptr, 0, 0, bounds.right, bounds.bottom,
                     SWP_NOACTIVATE | SWP_NOZORDER);
      }
    } else if (message == WM_NCDESTROY) {
      RemoveWindowSubclass(parent, ParentProc, id);
      self.parent_ = nullptr;
      self.window_ = nullptr;
    }
    return DefSubclassProc(parent, message, wparam, lparam);
  }
  HWND parent_ = nullptr;
  HWND window_ = nullptr;
  SIZE size_{};
};

namespace {
struct ThreadInput {
  HHOOK hook = nullptr;
  HHOOK focus_hook = nullptr;
  std::vector<NativeInput*> windows;
  std::vector<HWND> targets;
  bool forwarding_key = false;
};
thread_local ThreadInput input;
}  // namespace

NativeInputTarget::~NativeInputTarget() {
  Reset();
}

Result<HWND> NativeInputTarget::IslandParent(HWND browser) {
  if (!island_host_) {
    auto host = IslandInputHost::Acquire(browser);
    if (!host)
      return std::unexpected(host.error());
    island_host_ = std::move(*host);
  }
  return island_host_->Window();
}

void NativeInputTarget::Raise() const noexcept {
  if (island_host_)
    island_host_->Raise();
}

Result<void> NativeInputTarget::Initialize(HWND window) {
  if (window_ || !IsWindow(window) ||
      GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() ||
      std::ranges::find(input.targets, window) != input.targets.end())
    return Fail(ErrorCode::kInvalidArgument,
                "Native input requires a unique window on the owning thread");
  input.targets.push_back(window);
  window_ = window;
  return {};
}

void NativeInputTarget::Reset() noexcept {
  if (!window_)
    return;
  std::erase(input.targets, window_);
  window_ = nullptr;
}

bool NativeInputTarget::Contains(HWND window) noexcept {
  for (HWND current = window; current;
       current = GetWindow(GetAncestor(current, GA_ROOT), GW_OWNER))
    for (const auto target : input.targets)
      if (current == target || IsChild(target, current))
        return true;
  return false;
}

bool NativeInputTarget::Owns(HWND window) const noexcept {
  if (!window_)
    return false;
  for (HWND current = window; current;
       current = GetWindow(GetAncestor(current, GA_ROOT), GW_OWNER))
    if (current == window_ || IsChild(window_, current))
      return true;
  return false;
}

void ReleaseFocus(HWND content, HWND fallback, HWND host) noexcept {
  const HWND focus = GetFocus();
  if (!content || !focus || (focus != content && !IsChild(content, focus)))
    return;
  const auto usable = [content](HWND window) {
    return window && window != content && !IsChild(content, window) &&
           IsWindowVisible(window);
  };
  if (usable(fallback))
    SetFocus(fallback);
  else if (usable(host))
    SetFocus(host);
}

NativeInput::~NativeInput() {
  Reset();
}

Result<void> NativeInput::Initialize(HWND parent,
                                     void* context,
                                     Preprocess preprocess,
                                     UnhandledKey unhandled,
                                     FocusChanged focused) {
  if (parent_ || !IsWindow(parent) ||
      GetWindowThreadProcessId(parent, nullptr) != GetCurrentThreadId() ||
      !preprocess)
    return Fail(ErrorCode::kInvalidArgument,
                "Input requires an owning UI thread");
  // Allocate before installing the first hook, so failure cannot strand it.
  input.windows.push_back(this);
  if (!input.hook) {
    input.hook = SetWindowsHookExW(WH_GETMESSAGE, GetMessageHook, nullptr,
                                   GetCurrentThreadId());
    if (!input.hook) {
      input.windows.pop_back();
      return Fail(ErrorCode::kPlatform, "Could not install UI message hook");
    }
  }
  if (!input.focus_hook) {
    input.focus_hook =
        SetWindowsHookExW(WH_CBT, FocusHook, nullptr, GetCurrentThreadId());
    if (!input.focus_hook) {
      input.windows.pop_back();
      if (input.windows.empty()) {
        UnhookWindowsHookEx(input.hook);
        input.hook = nullptr;
      }
      return Fail(ErrorCode::kPlatform, "Could not install UI focus hook");
    }
  }
  parent_ = parent;
  context_ = context;
  preprocess_ = preprocess;
  unhandled_ = unhandled;
  focused_ = focused;
  return {};
}

void NativeInput::Reset() noexcept {
  if (!parent_)
    return;
  std::erase(input.windows, this);
  if (input.windows.empty()) {
    if (input.hook)
      UnhookWindowsHookEx(input.hook);
    if (input.focus_hook)
      UnhookWindowsHookEx(input.focus_hook);
    input.hook = nullptr;
    input.focus_hook = nullptr;
  }
  parent_ = nullptr;
  context_ = nullptr;
  preprocess_ = nullptr;
  unhandled_ = nullptr;
  focused_ = nullptr;
  last_key_ = {};
  key_forwarded_ = false;
}

bool NativeInput::Owns(HWND window) const noexcept {
  if (!window || !parent_)
    return false;
  if (window == parent_ || IsChild(parent_, window))
    return true;
  // Walk intermediate owners too: a browser can itself be an owned window.
  for (HWND root = GetAncestor(window, GA_ROOT); root;
       root = GetWindow(root, GW_OWNER))
    if (root == parent_ || IsChild(parent_, root))
      return true;
  return false;
}

NativeInput* NativeInput::FindOwner(HWND window) noexcept {
  // Pick the nearest registered owner, including intermediate owned windows.
  for (HWND root = GetAncestor(window, GA_ROOT); root;
       root = GetWindow(root, GW_OWNER))
    for (auto* candidate : input.windows)
      if (candidate->parent_ == root || IsChild(candidate->parent_, root))
        return candidate;
  return nullptr;
}

LRESULT CALLBACK NativeInput::FocusHook(int code,
                                        WPARAM window,
                                        LPARAM previous) noexcept {
  const HWND target = reinterpret_cast<HWND>(window);
  if (code == HCBT_SETFOCUS && NativeInputTarget::Contains(target))
    if (auto* owner = FindOwner(target); owner && owner->focused_) {
      const auto callback = owner->focused_;
      void* context = owner->context_;
      try {
        callback(context, target);
      } catch (...) {
        OutputDebugStringW(L"KryptonUI focus notification failed\n");
      }
    }
  return CallNextHookEx(nullptr, code, window, previous);
}

LRESULT CALLBACK NativeInput::GetMessageHook(int code,
                                             WPARAM remove,
                                             LPARAM packet) noexcept {
  if (code >= 0 && (remove & PM_REMOVE) != 0 && packet) {
    auto& message = *reinterpret_cast<MSG*>(packet);
    if (auto* owner = FindOwner(message.hwnd)) {
      const bool key =
          message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN;
      if (key) {
        owner->last_key_ = message;
        owner->key_forwarded_ = false;
      }
      // Copy the call target: a callback may unregister its own window.
      const auto preprocess = owner->preprocess_;
      void* context = owner->context_;
      const HWND parent = owner->parent_;
      const HWND previous_focus =
          key && NativeInputTarget::Contains(message.hwnd) ? GetFocus()
                                                           : nullptr;
      try {
        if (preprocess(context, message)) {
          message.message = WM_NULL;
          message.wParam = 0;
          message.lParam = 0;
        } else if (previous_focus) {
          // Ending pointer focus can return the keyboard to the page before
          // this key is dispatched. Preserve its original timestamp, repeat,
          // modifiers and subsequent character translation at that recipient.
          const HWND focus = GetFocus();
          if (focus && focus != previous_focus &&
              (focus == parent || IsChild(parent, focus)) &&
              !NativeInputTarget::Contains(focus))
            message.hwnd = focus;
        }
      } catch (...) {
        OutputDebugStringW(L"KryptonUI message preprocessing failed\n");
      }
    }
  }
  return CallNextHookEx(nullptr, code, remove, packet);
}

bool NativeInput::ForwardUnhandledKey(std::uint32_t key) const noexcept {
  if (!unhandled_ || key_forwarded_ || input.forwarding_key ||
      !Owns(last_key_.hwnd) || !NativeInputTarget::Contains(last_key_.hwnd) ||
      last_key_.wParam != key || key == VK_PROCESSKEY || key == VK_PACKET ||
      last_key_.time != static_cast<DWORD>(GetMessageTime()))
    return false;
  const MSG message = last_key_;
  const auto callback = unhandled_;
  void* context = context_;
  key_forwarded_ = true;
  input.forwarding_key = true;
  struct Scope {
    ~Scope() { input.forwarding_key = false; }
  } scope;
  try {
    return callback(context, message);
  } catch (...) {
    OutputDebugStringW(L"KryptonUI shortcut fallback failed\n");
    return false;
  }
}

}  // namespace kr::win
