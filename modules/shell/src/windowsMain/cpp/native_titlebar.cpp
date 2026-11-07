#include "native_titlebar.hpp"

#include <dwmapi.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Windows.Foundation.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <utility>

#include "krypton/ui/core/env.hpp"
#include "krypton/ui/platform/platform.hpp"
#include "krypton/ui/shell/render_stats.hpp"
#include "xaml_factory.hpp"

namespace kr::win {
namespace {
constexpr wchar_t kNonRudeWindowProperty[] = L"NonRudeHWND";
}  // namespace

namespace windowing = winrt::Microsoft::UI::Windowing;
using winrt::Microsoft::UI::Input::InputNonClientPointerSource;
using winrt::Microsoft::UI::Input::NonClientRegionKind;
void AppWindowChangedRevoker::Subscribe(
    const windowing::AppWindow& source,
    const winrt::Windows::Foundation::TypedEventHandler<
        windowing::AppWindow,
        windowing::AppWindowChangedEventArgs>& handler) {
  Reset();
  token_ = source.Changed(handler);
  source_ = source;
}

void AppWindowChangedRevoker::Reset() noexcept {
  if (source_) {
    source_.Changed(token_);
    source_ = nullptr;
    token_ = {};
  }
}

Result<void> NativeTitleBar::Initialize(HWND parent,
                                        ShellViewDelegate& delegate) noexcept {
  try {
    profile_ = EnvFlag("KR_UI_STATS");
    if (!windowing::AppWindowTitleBar::IsCustomizationSupported())
      return Fail(ErrorCode::kUnsupported,
                  "Native title bar customization is unavailable");
    parent_ = parent;
    delegate_ = &delegate;
    scale_ =
        static_cast<float>(GetDpiForWindow(parent_)) / USER_DEFAULT_SCREEN_DPI;
    window_ = windowing::AppWindow::GetFromWindowId(
        winrt::Microsoft::UI::GetWindowIdFromWindow(parent_));
    bar_ = window_.TitleBar();
    BOOL dark = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(parent_, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                        &dark, sizeof(dark))))
      previous_dark_ = dark;
    COLORREF border = DWMWA_COLOR_DEFAULT;
    if (SUCCEEDED(DwmGetWindowAttribute(parent_, DWMWA_BORDER_COLOR, &border,
                                        sizeof(border))))
      previous_border_ = border;
    previous_extended_ = bar_.ExtendsContentIntoTitleBar();
    applied_dark_ = previous_dark_;
    applied_border_ = previous_border_;
    previous_height_ = bar_.PreferredHeightOption();
    bar_.ExtendsContentIntoTitleBar(true);
    bar_.PreferredHeightOption(windowing::TitleBarHeightOption::Standard);
    bar_.IconShowOptions(windowing::IconShowOptions::HideIconAndSystemMenu);
    input_ = InputNonClientPointerSource::GetForWindowId(window_.Id());
    lifetime_ = std::make_shared<int>(0);
    const std::weak_ptr<int> weak = lifetime_;
    metrics_changed_.Subscribe(
        window_,
        [this, weak](auto&&, const windowing::AppWindowChangedEventArgs& args) {
          if (weak.expired())
            return;
          if (args.DidPresenterChange())
            AllowHostFullscreen();
          PublishMetrics();
          if (args.DidSizeChange() || args.DidPresenterChange())
            PassInputThrough();
        });
    AllowHostFullscreen();
    PublishMetrics();
    PassInputThrough();
    return {};
  } catch (const winrt::hresult_error&) {
    Reset();
    return Fail(ErrorCode::kPlatform,
                "Could not configure the native title bar");
  } catch (...) {
    Reset();
    return Fail(ErrorCode::kPlatform,
                "Could not initialize the native title bar");
  }
}

NativeTitleBar::~NativeTitleBar() {
  Reset();
}

void NativeTitleBar::Reset() noexcept {
  lifetime_.reset();
  metrics_changed_.Reset();
  delegate_ = nullptr;
  try {
    if (input_ && IsWindow(parent_))
      input_.ClearRegionRects(NonClientRegionKind::Passthrough);
    if (bar_ && IsWindow(parent_)) {
      bar_.PreferredHeightOption(previous_height_);
      bar_.ExtendsContentIntoTitleBar(previous_extended_);
      if (previous_dark_)
        DwmSetWindowAttribute(parent_, DWMWA_USE_IMMERSIVE_DARK_MODE,
                              &*previous_dark_, sizeof(BOOL));
      if (previous_border_)
        DwmSetWindowAttribute(parent_, DWMWA_BORDER_COLOR, &*previous_border_,
                              sizeof(COLORREF));
    }
  } catch (...) {
    // The host can already be destroying its non-client area.
  }
  if (non_rude_marker_ && IsWindow(parent_) &&
      !GetPropW(parent_, kNonRudeWindowProperty))
    SetPropW(parent_, kNonRudeWindowProperty, non_rude_marker_);
  non_rude_marker_ = nullptr;
  parent_ = nullptr;
  bar_ = nullptr;
  input_ = nullptr;
  standard_metrics_ = {};
  caption_visible_ = true;
  window_ = nullptr;
  previous_dark_.reset();
  previous_border_.reset();
  applied_dark_.reset();
  applied_border_.reset();
  applied_palette_.reset();
}

void NativeTitleBar::AllowHostFullscreen() noexcept {
  // AppWindow's overlapped presenter marks its HWND NonRudeHWND, keeping the
  // taskbar above even a correctly sized Chromium fullscreen window. We borrow
  // AppWindow only for caption controls; the host owns presentation and taskbar
  // policy. Preserve the SDK's marker for detach without changing presenters,
  // window styles, bounds, activation, or Chromium's background-fullscreen
  // hack.
  if (const auto marker = RemovePropW(parent_, kNonRudeWindowProperty);
      marker && !non_rude_marker_)
    non_rude_marker_ = marker;
}

void NativeTitleBar::SetScale(float scale) {
  if (scale_ != scale) {
    scale_ = scale;
    PublishMetrics();
  }
}

Result<void> NativeTitleBar::SetCaptionVisible(bool visible) noexcept {
  if (!bar_ || caption_visible_ == visible)
    return {};
  try {
    bar_.PreferredHeightOption(
        visible ? windowing::TitleBarHeightOption::Standard
                : windowing::TitleBarHeightOption::Collapsed);
    AllowHostFullscreen();
    caption_visible_ = visible;
    PublishMetrics();
    PassInputThrough();
    return {};
  } catch (...) {
    return Fail(ErrorCode::kPlatform,
                "Could not update native caption visibility");
  }
}

void NativeTitleBar::PublishMetrics() {
  if (!bar_ || !delegate_)
    return;
  // Collapsed captions report zero extents. Retain their measured standard
  // bounds so the shared reveal region does not shrink while they are hidden.
  if (bar_.PreferredHeightOption() !=
          windowing::TitleBarHeightOption::Collapsed &&
      bar_.Height() > 0) {
    standard_metrics_ = {
        .enabled = true,
        .height = static_cast<float>(bar_.Height()) / scale_,
        .left_inset = static_cast<float>(bar_.LeftInset()) / scale_,
        .right_inset = static_cast<float>(bar_.RightInset()) / scale_};
  }
  standard_metrics_.enabled = true;
  delegate_->OnNativeTitleBarChanged(standard_metrics_);
}

void NativeTitleBar::PassInputThrough() noexcept try {
  if (!input_ || !bar_)
    return;
  // NativeFrame hit-tests the caption, resize edges and controls itself. The
  // SDK's non-client input window would otherwise make its whole title bar
  // band a caption, over the controls drawn there. Keep only the system
  // caption buttons. The region also sizes that window, so it stays within
  // the band and off the page and its drop targets.
  const auto height = bar_.Height();
  if (height <= 0) {
    input_.ClearRegionRects(NonClientRegionKind::Passthrough);
    return;
  }
  const auto width = window_.ClientSize().Width;
  const auto left = bar_.LeftInset();
  const auto right = std::max(left, width - bar_.RightInset());
  const winrt::Windows::Graphics::RectInt32 band{left, 0, right - left, height};
  input_.SetRegionRects(NonClientRegionKind::Passthrough, {band});
} catch (...) {
  platform::WriteDefaultLog(LogLevel::kWarning,
                            "Title bar input could not pass through");
}

void NativeTitleBar::ApplyTheme(const ThemeVm& theme, bool contrast) {
  if (!bar_)
    return;
  const BOOL dark = theme.dark && !contrast;
  const auto started = profile_ ? std::chrono::steady_clock::now()
                                : std::chrono::steady_clock::time_point{};
  if (!applied_dark_ || *applied_dark_ != dark) {
    if (SUCCEEDED(DwmSetWindowAttribute(parent_, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                        &dark, sizeof(dark))))
      applied_dark_ = dark;
  }
  const COLORREF border = contrast ? DWMWA_COLOR_DEFAULT : DWMWA_COLOR_NONE;
  const auto after_dark = profile_ ? std::chrono::steady_clock::now() : started;
  if (!applied_border_ || *applied_border_ != border) {
    if (SUCCEEDED(DwmSetWindowAttribute(parent_, DWMWA_BORDER_COLOR, &border,
                                        sizeof(border))))
      applied_border_ = border;
  }
  const auto palette = std::pair{theme.dark && !contrast, contrast};
  const auto after_border =
      profile_ ? std::chrono::steady_clock::now() : started;
  if (applied_palette_ && *applied_palette_ == palette)
    return;
  if (contrast) {
    bar_.ButtonBackgroundColor(nullptr);
    bar_.ButtonInactiveBackgroundColor(nullptr);
    bar_.ButtonForegroundColor(nullptr);
    bar_.ButtonInactiveForegroundColor(nullptr);
    bar_.ButtonHoverBackgroundColor(nullptr);
    bar_.ButtonHoverForegroundColor(nullptr);
    bar_.ButtonPressedBackgroundColor(nullptr);
    bar_.ButtonPressedForegroundColor(nullptr);
    applied_palette_ = palette;
    return;
  }
  const auto& colors =
      theme.dark ? kTokens.native_caption_dark : kTokens.native_caption_light;
  const auto* previous =
      applied_palette_ && !applied_palette_->second
          ? (applied_palette_->first ? &kTokens.native_caption_dark
                                     : &kTokens.native_caption_light)
          : nullptr;
  const auto color = [](Color value) {
    return winrt::box_value(NativeColor(value))
        .as<winrt::Windows::Foundation::IReference<
            winrt::Windows::UI::Color>>();
  };
  if (!previous) {
    const auto transparent = color(Color{0});
    bar_.ButtonBackgroundColor(transparent);
    bar_.ButtonInactiveBackgroundColor(transparent);
  }
  if (!previous || previous->foreground != colors.foreground) {
    const auto foreground = color(Color{colors.foreground});
    bar_.ButtonForegroundColor(foreground);
    bar_.ButtonHoverForegroundColor(foreground);
    bar_.ButtonPressedForegroundColor(foreground);
  }
  if (!previous || previous->inactive_foreground != colors.inactive_foreground)
    bar_.ButtonInactiveForegroundColor(
        color(Color{colors.inactive_foreground}));
  if (!previous || previous->hover != colors.hover)
    bar_.ButtonHoverBackgroundColor(color(Color{colors.hover}));
  if (!previous || previous->pressed != colors.pressed)
    bar_.ButtonPressedBackgroundColor(color(Color{colors.pressed}));
  applied_palette_ = palette;
  if (profile_) {
    const auto ended = std::chrono::steady_clock::now();
    if (ended - started > RenderStats::kBudget) {
      const auto ms = [](auto value) {
        return std::chrono::duration<double, std::milli>(value).count();
      };
      platform::WriteDefaultLog(
          LogLevel::kWarning,
          std::format("native caption update: dark {:.3f}, border {:.3f}, "
                      "button colors {:.3f} ms",
                      ms(after_dark - started), ms(after_border - after_dark),
                      ms(ended - after_border)));
    }
  }
}

}  // namespace kr::win
