#pragma once

#include <windows.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include <cstdint>
#include <memory>
#include <span>

#include "krypton/ui/core/result.hpp"

namespace kr::win {

class IslandInputHost;

// Each retained island has its own coordinate space and popup collection.
// Cache the XamlRoot only after connection; registration ends before teardown.
struct NativeInputRoot {
  NativeInputRoot(HWND native,
                  const winrt::Microsoft::UI::Xaml::FrameworkElement& content)
      : window(native), element(content) {}
  [[nodiscard]] winrt::Microsoft::UI::Xaml::XamlRoot XamlRoot() const {
    if (!root_ && element)
      root_ = element.XamlRoot();
    return root_;
  }
  HWND window = nullptr;
  winrt::Microsoft::UI::Xaml::FrameworkElement element{nullptr};

 private:
  mutable winrt::Microsoft::UI::Xaml::XamlRoot root_{nullptr};
};

// Registers native UI content, never the browser's host HWND. Descendants
// and owned popups belong to the registration for its lifetime.
class NativeInputTarget {
 public:
  NativeInputTarget() = default;
  ~NativeInputTarget();
  NativeInputTarget(const NativeInputTarget&) = delete;
  NativeInputTarget& operator=(const NativeInputTarget&) = delete;

  [[nodiscard]] Result<void> Initialize(
      HWND window,
      const winrt::Microsoft::UI::Xaml::FrameworkElement& root);

  // XAML subscribes to its parent's non-client pointer source. Give islands a
  // borderless child parent so browser caption input cannot seed their replay
  // caches, including while an island is hidden. Coordinates stay client-local.
  [[nodiscard]] Result<HWND> IslandParent(HWND browser);
  void Raise() const noexcept;
  void Reset() noexcept;
  [[nodiscard]] HWND Window() const noexcept { return window_; }
  [[nodiscard]] bool Owns(HWND window) const noexcept;
  [[nodiscard]] static bool Contains(HWND window) noexcept;
  // Borrow only during a synchronous notification on the owning UI thread.
  [[nodiscard]] static std::span<const NativeInputRoot> Roots() noexcept;

 private:
  std::shared_ptr<IslandInputHost> island_host_;
  HWND window_ = nullptr;
};

// Win32 leaves keyboard focus on a child window that is hidden, so keys would
// keep reaching invisible content. Before `content` hides, moves focus held
// inside it to `fallback`, or to `host` when that window is gone or hidden.
void ReleaseFocus(HWND content, HWND fallback, HWND host) noexcept;

// One message hook and one focus hook per UI thread, shared by its shells.
// Registration and callbacks are confined to the window's owning thread.
class NativeInput {
 public:
  using Preprocess = bool (*)(void*, const MSG&);
  using UnhandledKey = bool (*)(void*, const MSG&);
  using FocusChanged = void (*)(void*, HWND);

  NativeInput() = default;
  ~NativeInput();
  NativeInput(const NativeInput&) = delete;
  NativeInput& operator=(const NativeInput&) = delete;

  [[nodiscard]] Result<void> Initialize(HWND parent,
                                        void* context,
                                        Preprocess preprocess,
                                        UnhandledKey unhandled,
                                        FocusChanged focused = nullptr);
  void Reset() noexcept;

  // A WinUI accelerator event reaches this after local key handlers decline
  // it. Forward the original queued message, not a reconstructed key packet.
  [[nodiscard]] bool ForwardUnhandledKey(std::uint32_t key) const noexcept;
  [[nodiscard]] bool Owns(HWND window) const noexcept;

 private:
  [[nodiscard]] static NativeInput* FindOwner(HWND window) noexcept;
  static LRESULT CALLBACK FocusHook(int code,
                                    WPARAM window,
                                    LPARAM previous) noexcept;
  static LRESULT CALLBACK GetMessageHook(int code,
                                         WPARAM remove,
                                         LPARAM message) noexcept;
  HWND parent_ = nullptr;
  void* context_ = nullptr;
  Preprocess preprocess_ = nullptr;
  UnhandledKey unhandled_ = nullptr;
  FocusChanged focused_ = nullptr;
  MSG last_key_{};
  mutable bool key_forwarded_ = false;
};

}  // namespace kr::win
