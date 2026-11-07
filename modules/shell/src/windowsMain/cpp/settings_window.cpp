#include "settings_window.hpp"

#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.System.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "krypton/ui/platform/platform.hpp"
#include "native_input.hpp"

namespace kr::win {

SettingsWindow::~SettingsWindow() {
  input_target_.Reset();
  fade_.Stop();
  lifetime_.reset();
  activated_.revoke();
  selected_.revoke();
  key_.revoke();
  fallback_key_.revoke();
  searched_.revoke();
  submitted_.revoke();
  resized_.revoke();
  try {
    if (closing_registered_)
      app_window_.Closing(closing_);
    if (window_)
      window_.Close();
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Settings window could not close");
  }
}

void SettingsWindow::Build() {
  if (window_)
    return;
  // The module runtime already owns the one process-wide XAML Application.
  window_ = xaml::Window();
  window_.SystemBackdrop(media::MicaBackdrop());
  app_window_ = window_.AppWindow();
  app_window_.TitleBar().IconShowOptions(
      winrt::Microsoft::UI::Windowing::IconShowOptions::HideIconAndSystemMenu);
  HWND native = nullptr;
  winrt::check_hresult(
      window_.as<::IWindowNative>()->get_WindowHandle(&native));
  native_ = native;
  winrt::check_bool(input_target_.Initialize(native).has_value());
  SetLastError(0);
  if (!SetWindowLongPtrW(native, GWLP_HWNDPARENT,
                         reinterpret_cast<LONG_PTR>(owner_)))
    winrt::check_bool(GetLastError() == 0);
  navigation_ = controls::NavigationView();
  navigation_.IsSettingsVisible(false);
  navigation_.IsBackButtonVisible(
      controls::NavigationViewBackButtonVisible::Collapsed);
  navigation_.PaneDisplayMode(controls::NavigationViewPaneDisplayMode::Auto);
  navigation_.OpenPaneLength(kTokens.settings.navigation_width);
  navigation_.ExpandedModeThresholdWidth(kTokens.settings.expanded_threshold);
  navigation_.CompactModeThresholdWidth(kTokens.settings.compact_threshold);
  navigation_.IsTitleBarAutoPaddingEnabled(false);
  navigation_.IsPaneOpen(true);
  navigation_.FontSize(kTokens.settings.body_size);
  root_ = controls::Grid();
  backdrop_.Root().Opacity(kTokens.settings.navigation_tint_opacity);
  root_.Children().Append(backdrop_.Root());
  root_.Children().Append(navigation_);
  content_background_ = style_.background;
  accent_ = media::SolidColorBrush();
  accent_text_ = media::SolidColorBrush();
  for (const auto key : {L"TextOnAccentFillColorPrimaryBrush",
                         L"TextOnAccentFillColorSecondaryBrush"})
    root_.Resources().Insert(winrt::box_value(key), accent_text_);
  navigation_.FontFamily(
      media::FontFamily(winrt::to_hstring(kTokens.settings.font_family)));
  for (const auto key :
       {L"AccentFillColorDefaultBrush", L"AccentFillColorSecondaryBrush",
        L"AccentFillColorTertiaryBrush", L"SystemControlHighlightAccentBrush",
        L"NavigationViewSelectionIndicatorForeground", L"ToggleSwitchFillOn",
        L"RadioButtonForegroundChecked", L"ComboBoxBorderBrushFocused",
        L"TextControlBorderBrushFocused"})
    root_.Resources().Insert(winrt::box_value(key), accent_);
  navigation_.Resources().Insert(
      winrt::box_value(L"NavigationViewDefaultPaneBackground"),
      context_.brushes.transparent);
  navigation_.Resources().Insert(
      winrt::box_value(L"NavigationViewExpandedPaneBackground"),
      context_.brushes.transparent);
  navigation_.Resources().Insert(
      winrt::box_value(L"NavigationViewContentBackground"),
      content_background_);
  search_ = controls::AutoSuggestBox();
  search_.Margin(UniformThickness(kTokens.settings.search_margin));
  search_.QueryIcon(controls::SymbolIcon(controls::Symbol::Find));
  navigation_.PaneFooter(search_);
  resized_ = root_.SizeChanged(winrt::auto_revoke,
                               [this](auto&&, auto&&) { Backdrop(); });
  searched_ = search_.TextChanged(
      winrt::auto_revoke,
      [this](auto&&, const controls::AutoSuggestBoxTextChangedEventArgs& args) {
        if (args.Reason() ==
            controls::AutoSuggestionBoxTextChangeReason::UserInput)
          Search();
      });
  submitted_ = search_.QuerySubmitted(
      winrt::auto_revoke,
      [this](auto&&,
             const controls::AutoSuggestBoxQuerySubmittedEventArgs& args) {
        std::uint32_t index = 0;
        if (args.ChosenSuggestion() &&
            (!suggestions_ ||
             !suggestions_.IndexOf(args.ChosenSuggestion(), index)))
          return;
        if (index >= results_.size())
          return;
        const auto entry = results_[index].entry;
        search_.IsSuggestionListOpen(false);
        focus_target_ = entry.label;
        context_.delegate.OnSettingsSectionSelected(entry.section);
        if (navigation_.DisplayMode() !=
            controls::NavigationViewDisplayMode::Expanded)
          navigation_.IsPaneOpen(false);
      });
  content_ = controls::StackPanel();
  content_.Spacing(kTokens.settings.section_gap);
  content_.MaxWidth(kTokens.settings.content_max_width);
  content_.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  content_.Padding(
      {kTokens.settings.page_padding, kTokens.settings.page_top_padding,
       kTokens.settings.page_padding, kTokens.settings.page_padding});
  title_ = Label(L"", kTokens.settings.page_title_size);
  title_.FontWeight(kSemiBold);
  title_.FontFamily(
      media::FontFamily(winrt::to_hstring(kTokens.settings.font_family)));
  title_.TextWrapping(xaml::TextWrapping::Wrap);
  title_.Foreground(style_.text);
  title_.Margin({kTokens.settings.row_inset, 0, 0, 0});

  content_.Children().Append(title_);
  description_ = Label(L"", kTokens.settings.body_size);
  description_.FontFamily(
      media::FontFamily(winrt::to_hstring(kTokens.settings.font_family)));
  description_.TextWrapping(xaml::TextWrapping::Wrap);
  description_.Foreground(context_.brushes.muted);
  description_.Visibility(xaml::Visibility::Collapsed);
  content_.Children().Append(description_);
  error_ = controls::InfoBar();
  error_.Severity(controls::InfoBarSeverity::Error);
  error_.IsClosable(false);
  content_.Children().Append(error_);
  empty_ = Label(L"", kTokens.settings.body_size);
  empty_.TextWrapping(xaml::TextWrapping::Wrap);
  empty_.Foreground(context_.brushes.muted);
  content_.Children().Append(empty_);
  browser_ = std::make_unique<BrowserSettingsPanel>(context_, style_);
  content_.Children().Append(browser_->Root());
  scroll_ = controls::ScrollViewer();
  scroll_.HorizontalScrollBarVisibility(
      controls::ScrollBarVisibility::Disabled);
  scroll_.VerticalScrollBarVisibility(controls::ScrollBarVisibility::Auto);
  scroll_.Content(content_);
  navigation_.Content(scroll_);
  for (const auto& section : SettingsSections()) {
    controls::NavigationViewItem item;
    item.MinHeight(kTokens.settings.navigation_item_height);
    item.FontSize(kTokens.settings.body_size);
    item.Icon(controls::FontIcon());
    auto glyph = item.Icon().as<controls::FontIcon>();
    glyph.FontFamily(
        media::FontFamily(winrt::to_hstring(kTokens.type.icon_family)));
    glyph.Glyph(winrt::hstring(section.glyph));
    glyph.FontSize(kTokens.settings.navigation_icon_size);
    navigation_.MenuItems().Append(item);
    items_.push_back(item);
  }
  const std::weak_ptr<int> weak = lifetime_;
  activated_ = window_.Activated(
      winrt::auto_revoke,
      [this, weak](auto&&, const xaml::WindowActivatedEventArgs& args) {
        if (weak.expired() || !visible_ ||
            args.WindowActivationState() !=
                xaml::WindowActivationState::Deactivated ||
            (browser_ && browser_->DialogOpen()) ||
            (privacy_ && privacy_->DialogOpen()))
          return;
        // Opening and programmatic focus handoffs can also deactivate WinUI.
        // Only an actual outside press should light-dismiss this window.
        const bool pressed = std::ranges::any_of(
            std::array{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1,
                       VK_XBUTTON2},
            [](int key) { return GetAsyncKeyState(key) < 0; });
        POINT cursor{};
        if (pressed && GetCursorPos(&cursor))
          OnPointerPress(WindowFromPoint(cursor));
      });
  selected_ = navigation_.SelectionChanged(
      winrt::auto_revoke, [this, weak](auto&&, auto&& args) {
        if (weak.expired() || reconciling_)
          return;
        for (std::size_t index = 0; index < items_.size(); ++index)
          if (items_[index] == args.SelectedItemContainer()) {
            context_.delegate.OnSettingsSectionSelected(
                SettingsSections()[index].section);
            return;
          }
      });
  key_ = root_.KeyDown(
      winrt::auto_revoke,
      [this, weak](auto&&, const xaml::Input::KeyRoutedEventArgs& args) {
        if (!weak.expired() &&
            args.Key() == winrt::Windows::System::VirtualKey::Escape) {
          if (browser_ && browser_->DialogOpen())
            return;
          Dismiss();
          args.Handled(true);
        }
      });
  fallback_key_ = root_.ProcessKeyboardAccelerators(
      winrt::auto_revoke,
      [this](auto&&,
             const xaml::Input::ProcessKeyboardAcceleratorEventArgs& args) {
        if (!args.Handled() && context_.input.ForwardUnhandledKey(
                                   static_cast<std::uint32_t>(args.Key())))
          args.Handled(true);
      });
  // AppWindow does not implement IWeakReferenceSource: own its explicit token.
  closing_ = app_window_.Closing([this, weak](auto&&, auto&& args) {
    if (weak.expired())
      return;
    args.Cancel(true);
    Dismiss();
  });
  closing_registered_ = true;
  title_bar_ = controls::Grid();
  title_bar_.Height(kTokens.settings.title_bar_height);
  title_bar_.VerticalAlignment(xaml::VerticalAlignment::Top);
  root_.Children().Append(title_bar_);
  window_.Content(root_);
  window_.ExtendsContentIntoTitleBar(true);
  window_.SetTitleBar(title_bar_);
  if (const auto presenter =
          app_window_.Presenter()
              .try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>()) {
    presenter.IsMaximizable(false);
    presenter.IsMinimizable(false);
    const auto scale =
        static_cast<float>(GetDpiForWindow(owner_)) / USER_DEFAULT_SCREEN_DPI;
    presenter.PreferredMinimumWidth(
        winrt::box_value(static_cast<int>(std::lround(
                             kTokens.settings.minimum_width * scale)))
            .as<foundation::IReference<int>>());
    presenter.PreferredMinimumHeight(
        winrt::box_value(static_cast<int>(std::lround(
                             kTokens.settings.minimum_height * scale)))
            .as<foundation::IReference<int>>());
  }
}

void SettingsWindow::OnPointerPress(HWND target) {
  if (!visible_ || !target || (browser_ && browser_->DialogOpen()) ||
      (privacy_ && privacy_->DialogOpen()))
    return;
  // Combo boxes, suggestions and owned popups are part of settings too.
  for (HWND window = GetAncestor(target, GA_ROOT); window;
       window = GetWindow(window, GW_OWNER))
    if (window == native_)
      return;
  context_.delegate.OnSettingsDeactivated();
}

void SettingsWindow::Dismiss() {
  if (!visible_)
    return;
  fade_.Stop();
  SavePosition();
  if (browser_)
    browser_->DismissDialog();
  visible_ = false;
  const auto window = app_window_;
  hide_.Hide(native_, root_.DispatcherQueue(), [window] { window.Hide(); });
  context_.delegate.OnSettingsDismissed();
}

void SettingsWindow::Render(const ShellViewModel& vm) {
  if (vm.changed.Has(Dirty::kText)) {
    text_ready_ = false;
    if (appearance_)
      appearance_->InvalidateText();
    if (privacy_)
      privacy_->InvalidateText();
  }
  if (!vm.settings.state.open) {
    if (privacy_)
      privacy_->Render(vm.privacy_features, vm.text, false);
    if (visible_) {
      fade_.Stop();
      SavePosition();
      if (browser_)
        browser_->DismissDialog();
      visible_ = false;
      const auto window = app_window_;
      hide_.Hide(native_, root_.DispatcherQueue(), [window] { window.Hide(); });
    }
    return;
  }
  Build();
  hide_.BeforeShow();
  if (shown_section_ && *shown_section_ != vm.settings.state.section)
    SavePosition();
  reconciling_ = true;
  struct Reset {
    bool& value;
    ~Reset() { value = false; }
  } reset{reconciling_};
  theme_ = vm.theme;
  workspace_theme_ =
      vm.theme.workspace_theme
          ? std::optional<WorkspaceTheme>(*vm.theme.workspace_theme)
          : std::nullopt;
  theme_.workspace_theme = workspace_theme_ ? &*workspace_theme_ : nullptr;
  style_.Apply(vm.theme, context_.brushes);
  accent_.Color(context_.brushes.high_contrast()
                    ? context_.brushes.active.Color()
                    : NativeColor(vm.theme.accent));
  Backdrop();
  accent_text_.Color(
      context_.brushes.high_contrast()
          ? context_.brushes.text.Color()
          : NativeColor(Color{SettingsAccentUsesDarkText(vm.theme.accent)
                                  ? kTokens.settings.accent_dark_text
                                  : kTokens.settings.accent_light_text}));
  const auto theme =
      vm.theme.dark ? xaml::ElementTheme::Dark : xaml::ElementTheme::Light;
  if (root_.RequestedTheme() != theme)
    root_.RequestedTheme(theme);
  const auto title_theme =
      context_.brushes.high_contrast()
          ? winrt::Microsoft::UI::Windowing::TitleBarTheme::UseDefaultAppMode
      : vm.theme.dark ? winrt::Microsoft::UI::Windowing::TitleBarTheme::Dark
                      : winrt::Microsoft::UI::Windowing::TitleBarTheme::Light;
  if (title_theme_ != title_theme) {
    app_window_.TitleBar().PreferredTheme(title_theme);
    title_theme_ = title_theme;
  }
  const auto direction = vm.text.right_to_left
                             ? xaml::FlowDirection::RightToLeft
                             : xaml::FlowDirection::LeftToRight;
  if (root_.FlowDirection() != direction)
    root_.FlowDirection(direction);
  const bool refresh_text = !text_ready_ || vm.changed.Has(Dirty::kText);
  if (refresh_text) {
    window_.Title(NativeText(UiText::kSettingsTitle, vm.text));
    no_results_ = NativeText(UiText::kSettingsNoResults, vm.text);
    search_.PlaceholderText(
        NativeText(UiText::kSettingsSearchPlaceholder, vm.text));
    Accessible(search_,
               NativeText(UiText::kSettingsSearchPlaceholder, vm.text));
  }
  description_.Text(
      NativeText(SettingsDescription(vm.settings.state.section), vm.text));
  error_.Message(NativeText(UiText::kSettingsError, vm.text));
  error_.IsOpen(vm.settings.browser->error);
  error_.Visibility(vm.settings.browser->error ? xaml::Visibility::Visible
                                               : xaml::Visibility::Collapsed);
  bool has_content = false;
  bool catalog_changed =
      refresh_text || search_entries_.size() != vm.settings.entries.size();
  for (std::size_t i = 0; !catalog_changed && i < search_entries_.size(); ++i) {
    const auto& current = vm.settings.entries[i];
    const auto& previous = search_entries_[i].entry;
    catalog_changed = current.label != previous.label ||
                      current.description != previous.description ||
                      current.section != previous.section;
  }
  for (const auto& entry : vm.settings.entries)
    has_content |= entry.section == vm.settings.state.section;
  if (catalog_changed) {
    search_entries_.clear();
    for (const auto& entry : vm.settings.entries)
      search_entries_.push_back({entry, std::string(vm.text[entry.label]),
                                 std::string(vm.text[entry.description])});
    if (!search_.Text().empty())
      Search();
  }
  has_content |= vm.settings.state.section == SettingsSection::kPrivacy &&
                 !vm.privacy_features.rows.empty();
  empty_.Text(NativeText(UiText::kSettingsEmpty, vm.text));
  empty_.Visibility(has_content ? xaml::Visibility::Collapsed
                                : xaml::Visibility::Visible);
  browser_->Render(vm.settings, vm.text, refresh_text,
                   vm.changed.HasAny(Dirty::kSpaces | Dirty::kSettings));
  const auto sections = SettingsSections();
  for (std::size_t index = 0; index < sections.size(); ++index) {
    const auto& section = sections[index];
    const bool available =
        std::ranges::any_of(vm.settings.sections, [&](const auto& candidate) {
          return candidate.section == section.section;
        });
    const auto visibility =
        available ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
    if (items_[index].Visibility() != visibility)
      items_[index].Visibility(visibility);
    if (refresh_text)
      items_[index].Content(
          winrt::box_value(NativeText(section.label, vm.text)));
    if (section.section == vm.settings.state.section) {
      if (navigation_.SelectedItem() != items_[index])
        navigation_.SelectedItem(items_[index]);
      const auto label = NativeText(section.label, vm.text);
      if (title_.Text() != label)
        title_.Text(label);
    }
  }
  if (vm.settings.appearance_visible) {
    if (!appearance_) {
      appearance_ = std::make_unique<AppearanceSettings>(context_, style_);
      content_.Children().InsertAt(3, appearance_->Root());
    }
    appearance_->Root().Visibility(vm.settings.state.appearance_editable ||
                                           vm.settings.state.layout_editable
                                       ? xaml::Visibility::Visible
                                       : xaml::Visibility::Collapsed);
    appearance_->Render(vm.settings, vm.text, vm.theme, refresh_text);
  } else if (appearance_)
    appearance_->Root().Visibility(xaml::Visibility::Collapsed);
  const bool privacy = vm.settings.state.section == SettingsSection::kPrivacy;
  if (privacy && !privacy_) {
    privacy_ = std::make_unique<PrivacySettings>(context_, style_);
    content_.Children().InsertAt(3, privacy_->Root());
  }
  if (privacy_) {
    privacy_->Render(vm.privacy_features, vm.text, refresh_text);
    privacy_->Root().Visibility(privacy && !vm.privacy_features.rows.empty()
                                    ? xaml::Visibility::Visible
                                    : xaml::Visibility::Collapsed);
  }
  text_ready_ = true;
  if (visible_ && content_.IsLoaded() &&
      shown_section_ != vm.settings.state.section)
    fade_.Play(content_, context_.motion, context_.brushes.high_contrast(),
               kTokens.motion.normal_ms);
  const bool section_changed = shown_section_ != vm.settings.state.section;
  shown_section_ = vm.settings.state.section;
  if (section_changed && visible_)
    content_.UpdateLayout();
  if (section_changed && visible_)
    scroll_.ChangeView(nullptr, offsets_[std::to_underlying(*shown_section_)],
                       nullptr, true);
  if (focus_target_) {
    content_.UpdateLayout();
    if (!browser_->Focus(*focus_target_) && appearance_ &&
        vm.settings.appearance_visible)
      appearance_->Focus(*focus_target_);
    focus_target_.reset();
  }
  if (!visible_ || open_serial_ != vm.settings.state.open_serial) {
    open_serial_ = vm.settings.state.open_serial;
    if (!visible_)
      Position();
    visible_ = true;
    window_.Activate();
    if (const auto previous =
            focus_[std::to_underlying(vm.settings.state.section)].get())
      previous.Focus(xaml::FocusState::Programmatic);
  }
}
void SettingsWindow::SavePosition() {
  if (!shown_section_)
    return;
  const auto index = std::to_underlying(*shown_section_);
  offsets_[index] = scroll_.VerticalOffset();
  if (root_.XamlRoot()) {
    if (const auto focused =
            xaml::Input::FocusManager::GetFocusedElement(root_.XamlRoot())
                .try_as<xaml::UIElement>())
      focus_[index] = winrt::make_weak(focused);
  }
}
void SettingsWindow::Backdrop() {
  const auto width = static_cast<float>(root_.ActualWidth());
  const auto height = static_cast<float>(root_.ActualHeight());
  backdrop_.Render(theme_, {0, 0, width, height}, {0, 0, width, height},
                   context_.brushes.high_contrast());
  if (width > 0) {
    const bool expanded = width >= kTokens.settings.expanded_threshold;
    navigation_.IsPaneToggleButtonVisible(!expanded);
    const float padding = expanded ? kTokens.settings.page_padding
                                   : kTokens.settings.narrow_padding;
    const xaml::Thickness insets{padding, kTokens.settings.page_top_padding,
                                 padding, padding};
    if (content_.Padding() != insets)
      content_.Padding(insets);
    const float pane =
        expanded ? kTokens.settings.navigation_width
                 : static_cast<float>(navigation_.CompactPaneLength());
    title_bar_.Margin(root_.FlowDirection() == xaml::FlowDirection::RightToLeft
                          ? xaml::Thickness{0, 0, pane, 0}
                          : xaml::Thickness{pane, 0, 0, 0});
  }
}
void SettingsWindow::Position() {
  RECT owner{};
  MONITORINFO monitor{sizeof(MONITORINFO)};
  if (!GetWindowRect(owner_, &owner) ||
      !GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST),
                       &monitor))
    return;
  const float scale =
      static_cast<float>(GetDpiForWindow(owner_)) / USER_DEFAULT_SCREEN_DPI;
  const auto dips = [scale](const RECT& bounds) {
    return Rect{bounds.left / scale, bounds.top / scale,
                (bounds.right - bounds.left) / scale,
                (bounds.bottom - bounds.top) / scale};
  };
  const auto bounds = SettingsWindowBounds(dips(owner), dips(monitor.rcWork));
  const auto pixels = [scale](float value) {
    return static_cast<int>(std::lround(value * scale));
  };
  app_window_.MoveAndResize({pixels(bounds.x), pixels(bounds.y),
                             pixels(bounds.width), pixels(bounds.height)});
}
void SettingsWindow::Search() {
  results_.clear();
  suggestions_ =
      winrt::single_threaded_observable_vector<foundation::IInspectable>();
  const auto query = winrt::to_string(search_.Text());
  if (!query.empty())
    for (const auto& candidate : search_entries_) {
      if (SettingsTextMatches(candidate.label, query) ||
          SettingsTextMatches(candidate.description, query)) {
        results_.push_back(candidate);
        suggestions_.Append(
            winrt::box_value(winrt::to_hstring(candidate.label)));
      }
    }
  if (!query.empty() && results_.empty())
    suggestions_.Append(winrt::box_value(no_results_));
  search_.ItemsSource(suggestions_);
}
}  // namespace kr::win
