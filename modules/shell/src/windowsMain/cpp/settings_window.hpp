#pragma once

#include <windows.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <optional>

#include "island_hide.hpp"
#include "native_input.hpp"
#include "settings_appearance.hpp"
#include "settings_browser.hpp"
#include "settings_privacy.hpp"
#include "settings_style.hpp"
#include "theme_backdrop.hpp"

namespace kr::win {
class SettingsWindow {
 public:
  SettingsWindow(HWND owner, ViewContext& context)
      : owner_(owner), context_(context) {}
  ~SettingsWindow();
  SettingsWindow(const SettingsWindow&) = delete;
  SettingsWindow& operator=(const SettingsWindow&) = delete;
  void Render(const ShellViewModel& vm);
  void OnPointerPress(HWND target);

 private:
  void Build();
  void Dismiss();
  void Search();
  void SavePosition();
  void Backdrop();
  void Position();
  SettingsStyle style_;
  ThemeBackdrop backdrop_;
  ThemeVm theme_;
  std::optional<WorkspaceTheme> workspace_theme_;
  controls::AutoSuggestBox search_{nullptr};
  controls::TextBlock description_{nullptr}, empty_{nullptr};
  controls::InfoBar error_{nullptr};
  media::SolidColorBrush content_background_{nullptr}, accent_{nullptr},
      accent_text_{nullptr};
  std::unique_ptr<BrowserSettingsPanel> browser_;
  struct SearchEntry {
    SettingsEntry entry;
    std::string label, description;
  };
  std::vector<SearchEntry> search_entries_, results_;
  foundation::Collections::IObservableVector<foundation::IInspectable>
      suggestions_{nullptr};
  winrt::hstring no_results_;
  std::optional<UiText> focus_target_;
  std::array<double, 9> offsets_{};
  std::array<winrt::weak_ref<xaml::UIElement>, 9> focus_{};
  controls::AutoSuggestBox::TextChanged_revoker searched_;
  controls::AutoSuggestBox::QuerySubmitted_revoker submitted_;
  xaml::FrameworkElement::SizeChanged_revoker resized_;
  HWND owner_;
  HWND native_ = nullptr;
  NativeInputTarget input_target_;
  IslandHide hide_;
  ViewContext& context_;
  ContentFade fade_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  xaml::Window window_{nullptr};
  winrt::Microsoft::UI::Windowing::AppWindow app_window_{nullptr};
  controls::NavigationView navigation_{nullptr};
  controls::Grid root_{nullptr}, title_bar_{nullptr};
  controls::ScrollViewer scroll_{nullptr};
  controls::StackPanel content_{nullptr};
  controls::TextBlock title_{nullptr};
  std::unique_ptr<AppearanceSettings> appearance_;
  std::unique_ptr<PrivacySettings> privacy_;
  std::unique_ptr<NativeButton> permissions_;
  std::vector<controls::NavigationViewItem> items_;
  bool visible_ = false, reconciling_ = false, closing_registered_ = false,
       text_ready_ = false;
  std::uint32_t open_serial_ = 0;
  std::optional<SettingsSection> shown_section_;
  std::optional<winrt::Microsoft::UI::Windowing::TitleBarTheme> title_theme_;
  winrt::event_token closing_{};
  xaml::Window::Activated_revoker activated_;
  controls::NavigationView::SelectionChanged_revoker selected_;
  xaml::UIElement::KeyDown_revoker key_;
  xaml::UIElement::ProcessKeyboardAccelerators_revoker fallback_key_;
};
}  // namespace kr::win
