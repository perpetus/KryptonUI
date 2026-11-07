#pragma once

#include <windows.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Windows.Graphics.h>

#include <memory>
#include <optional>
#include <utility>

#include "krypton/ui/core/result.hpp"
#include "krypton/ui/platform/shell_view.hpp"

namespace kr::win {

// AppWindow is not weak-referenceable. Its generated auto-revoker would
// dereference a missing IWeakReferenceSource; retain the source and its token.
class AppWindowChangedRevoker {
 public:
  AppWindowChangedRevoker() = default;
  ~AppWindowChangedRevoker() { Reset(); }
  AppWindowChangedRevoker(const AppWindowChangedRevoker&) = delete;
  AppWindowChangedRevoker& operator=(const AppWindowChangedRevoker&) = delete;
  AppWindowChangedRevoker(AppWindowChangedRevoker&&) = delete;
  AppWindowChangedRevoker& operator=(AppWindowChangedRevoker&&) = delete;

  void Subscribe(
      const winrt::Microsoft::UI::Windowing::AppWindow& source,
      const winrt::Windows::Foundation::TypedEventHandler<
          winrt::Microsoft::UI::Windowing::AppWindow,
          winrt::Microsoft::UI::Windowing::AppWindowChangedEventArgs>& handler);
  void Reset() noexcept;

 private:
  winrt::Microsoft::UI::Windowing::AppWindow source_{nullptr};
  winrt::event_token token_{};
};

class NativeTitleBar {
 public:
  NativeTitleBar() = default;
  NativeTitleBar(const NativeTitleBar&) = delete;
  NativeTitleBar& operator=(const NativeTitleBar&) = delete;
  NativeTitleBar(NativeTitleBar&&) = delete;
  NativeTitleBar& operator=(NativeTitleBar&&) = delete;
  ~NativeTitleBar();
  void Reset() noexcept;
  [[nodiscard]] Result<void> Initialize(HWND parent,
                                        ShellViewDelegate& delegate) noexcept;
  void SetScale(float scale);
  void ApplyTheme(const ThemeVm& theme, bool high_contrast);
  [[nodiscard]] Result<void> SetCaptionVisible(bool visible) noexcept;
  [[nodiscard]] bool Enabled() const noexcept { return bar_ != nullptr; }

 private:
  void AllowHostFullscreen() noexcept;
  void PublishMetrics();
  void PassInputThrough() noexcept;
  HWND parent_ = nullptr;
  HANDLE non_rude_marker_ = nullptr;
  ShellViewDelegate* delegate_ = nullptr;
  winrt::Microsoft::UI::Windowing::AppWindow window_{nullptr};
  winrt::Microsoft::UI::Windowing::AppWindowTitleBar bar_{nullptr};
  winrt::Microsoft::UI::Input::InputNonClientPointerSource input_{nullptr};
  AppWindowChangedRevoker metrics_changed_;
  std::shared_ptr<int> lifetime_;
  bool previous_extended_ = false;
  winrt::Microsoft::UI::Windowing::TitleBarHeightOption previous_height_{};
  NativeTitleBarMetrics standard_metrics_;
  bool caption_visible_ = true;
  float scale_ = 1;
  std::optional<BOOL> previous_dark_;
  std::optional<COLORREF> previous_border_;
  std::optional<BOOL> applied_dark_;
  std::optional<COLORREF> applied_border_;
  std::optional<std::pair<bool, bool>> applied_palette_;
  bool profile_ = false;
};

}  // namespace kr::win
