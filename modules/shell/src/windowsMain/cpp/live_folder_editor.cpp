#include "live_folder_editor.hpp"

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <cmath>

#include "krypton/ui/model/ui_text.hpp"
#include "native_input.hpp"
#include "xaml_factory.hpp"

namespace kr::win {
namespace {
// Flyouts are windowed, so the sidebar island's narrow XamlRoot must not be
// treated as the form's viewport. Query the anchor's actual monitor once/show.
Rect WorkArea(const xaml::FrameworkElement& anchor) {
  constexpr auto& t = kTokens.live_folder_editor;
  const float inset = t.margin + t.padding + kTokens.menu.border_width;
  Rect result{0, 0, t.width + 2 * inset, t.max_height + 2 * inset};
  const auto root = anchor.XamlRoot();
  for (const auto& target : NativeInputTarget::Roots()) {
    if (!root || target.XamlRoot() != root)
      continue;
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(
            MonitorFromWindow(target.window, MONITOR_DEFAULTTONEAREST),
            &monitor)) {
      const float scale =
          std::max(1.0f, static_cast<float>(GetDpiForWindow(target.window)) /
                             USER_DEFAULT_SCREEN_DPI);
      result.width =
          static_cast<float>(monitor.rcWork.right - monitor.rcWork.left) /
          scale;
      result.height =
          static_cast<float>(monitor.rcWork.bottom - monitor.rcWork.top) /
          scale;
    }
    break;
  }
  return result;
}
}  // namespace

LiveFolderEditor::LiveFolderEditor(ViewContext& context) : context_(context) {
  controls::StackPanel content;
  content.Spacing(kTokens.spacing.section_gap);
  content.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  flyout_ = controls::Flyout();
  flyout_.AreOpenCloseAnimationsEnabled(false);
  flyout_.ShouldConstrainToRootBounds(false);
  provider_ = controls::ComboBox();
  provider_.Header(winrt::box_value(NativeText(UiText::kLiveFolderProvider)));
  for (const auto label : {UiText::kLiveFolderOff, UiText::kLiveFolderRss,
                           UiText::kLiveFolderPulls, UiText::kLiveFolderIssues})
    provider_.Items().Append(winrt::box_value(NativeText(label)));
  provider_.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  provider_.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
  provider_.MinWidth(0);
  source_ = controls::TextBox();
  source_.MaxLength(2048);
  source_.MinWidth(0);
  source_.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  interval_ = controls::NumberBox();
  interval_.Header(winrt::box_value(NativeText(UiText::kLiveFolderInterval)));
  interval_.Minimum(15);
  interval_.Maximum(480);
  interval_.SmallChange(15);
  interval_.MinWidth(0);
  interval_.SpinButtonPlacementMode(
      controls::NumberBoxSpinButtonPlacementMode::Compact);
  limit_ = controls::NumberBox();
  limit_.Header(winrt::box_value(NativeText(UiText::kLiveFolderLimit)));
  limit_.Minimum(1);
  limit_.Maximum(100);
  limit_.MinWidth(0);
  limit_.SpinButtonPlacementMode(
      controls::NumberBoxSpinButtonPlacementMode::Compact);
  help_ = controls::TextBlock();
  help_.Text(NativeText(UiText::kLiveFolderHelp));
  help_.TextWrapping(xaml::TextWrapping::Wrap);
  warning_ = controls::InfoBar();
  warning_.Severity(controls::InfoBarSeverity::Warning);
  warning_.IsClosable(false);
  warning_.Title(NativeText(UiText::kLiveFolderSignedOut));
  sign_in_ = controls::Button();
  sign_in_.Content(winrt::box_value(NativeText(UiText::kLiveFolderSignIn)));
  signed_in_ = sign_in_.Click(winrt::auto_revoke, [this](auto&&, auto&&) {
    const auto lifetime = std::weak_ptr(lifetime_);
    const auto queue = sign_in_.DispatcherQueue();
    flyout_.Hide();
    (void)queue.TryEnqueue([this, lifetime] {
      if (!lifetime.expired())
        context_.delegate.OnUrlDropped("https://github.com/login");
    });
  });
  warning_.ActionButton(sign_in_);
  status_ = controls::TextBlock();
  status_.TextWrapping(xaml::TextWrapping::Wrap);
  error_ = controls::TextBlock();
  error_.TextWrapping(xaml::TextWrapping::Wrap);
  save_ = controls::Button();
  save_.Content(winrt::box_value(NativeText(UiText::kSaveFolder)));
  save_.HorizontalAlignment(xaml::HorizontalAlignment::Right);
  saved_ = save_.Click(winrt::auto_revoke, [this](auto&&, auto&&) { Save(); });
  provider_changed_ = provider_.SelectionChanged(
      winrt::auto_revoke, [this](auto&&, auto&&) { UpdateProvider(); });
  content.Children().Append(provider_);
  content.Children().Append(source_);
  content.Children().Append(interval_);
  content.Children().Append(limit_);
  content.Children().Append(help_);
  content.Children().Append(warning_);
  content.Children().Append(status_);
  content.Children().Append(error_);
  content.Children().Append(save_);
  scroll_ = controls::ScrollViewer();
  scroll_.HorizontalScrollMode(controls::ScrollMode::Disabled);
  scroll_.HorizontalScrollBarVisibility(
      controls::ScrollBarVisibility::Disabled);
  scroll_.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
  scroll_.VerticalScrollMode(controls::ScrollMode::Auto);
  scroll_.VerticalScrollBarVisibility(controls::ScrollBarVisibility::Auto);
  scroll_.Content(content);
  flyout_.Content(scroll_);
}

LiveFolderEditor::~LiveFolderEditor() {
  lifetime_.reset();
  signed_in_.revoke();
  provider_changed_.revoke();
  saved_.revoke();
  try {
    flyout_.Hide();
  } catch (...) {
  }
}

void LiveFolderEditor::Show(const xaml::FrameworkElement& anchor,
                            const FolderVm& folder,
                            std::optional<LiveFolderProvider> preset) {
  folder_ = folder.id;
  const bool quick_feed = preset == LiveFolderProvider::kRss;
  sign_in_required_ = LiveFolderNeedsSignIn(folder);
  const auto advanced =
      quick_feed ? xaml::Visibility::Collapsed : xaml::Visibility::Visible;
  provider_.Visibility(advanced);
  interval_.Visibility(advanced);
  limit_.Visibility(advanced);
  help_.Visibility(advanced);
  const auto draft = PresentLiveFolderDraft(folder, preset);
  provider_.SelectedIndex(static_cast<int>(draft.provider));
  source_.Text(winrt::to_hstring(draft.source));
  interval_.Value(draft.interval_minutes);
  limit_.Value(draft.max_items);
  status_.Text(winrt::to_hstring(folder.live_status));
  status_.Visibility(quick_feed || folder.live_status.empty() ||
                             LiveFolderNeedsSignIn(folder)
                         ? xaml::Visibility::Collapsed
                         : xaml::Visibility::Visible);
  error_.Text(L"");
  error_.Visibility(xaml::Visibility::Collapsed);
  UpdateProvider();
  const auto metrics = MeasureLiveFolderEditor(WorkArea(anchor));
  if (width_ != metrics.width || max_height_ != metrics.max_height) {
    width_ = metrics.width;
    max_height_ = metrics.max_height;
    constexpr auto& t = kTokens.live_folder_editor;
    // A finite viewport remeasures wrapping text and controls instead of
    // clipping an unconstrained StackPanel to FlyoutPresenter's default cap.
    scroll_.Width(width_);
    scroll_.MaxHeight(max_height_);
    xaml::Style style(winrt::xaml_typename<controls::FlyoutPresenter>());
    style.Setters().Append(xaml::Setter(
        xaml::FrameworkElement::MinWidthProperty(), winrt::box_value(0.0)));
    style.Setters().Append(xaml::Setter(
        xaml::FrameworkElement::MaxWidthProperty(),
        winrt::box_value(static_cast<double>(
            width_ + 2 * (t.padding + kTokens.menu.border_width)))));
    style.Setters().Append(xaml::Setter(
        xaml::FrameworkElement::MaxHeightProperty(),
        winrt::box_value(static_cast<double>(
            max_height_ + 2 * (t.padding + kTokens.menu.border_width)))));
    style.Setters().Append(
        xaml::Setter(controls::Control::PaddingProperty(),
                     winrt::box_value(UniformThickness(t.padding))));
    style.Setters().Append(xaml::Setter(
        controls::Control::BorderThicknessProperty(),
        winrt::box_value(UniformThickness(kTokens.menu.border_width))));
    style.Setters().Append(
        xaml::Setter(controls::Control::BorderBrushProperty(),
                     context_.brushes.menu_border));
    flyout_.FlyoutPresenterStyle(style);
  }
  flyout_.ShowAt(anchor);
}

void LiveFolderEditor::UpdateProvider() {
  const auto index = provider_.SelectedIndex();
  if (index < 0 || index > static_cast<int>(LiveFolderProvider::kIssues))
    return;
  const auto fields =
      PresentLiveFolderFields(static_cast<LiveFolderProvider>(index));
  source_.Header(winrt::box_value(NativeText(fields.source_label)));
  source_.PlaceholderText(NativeText(fields.source_hint));
  warning_.IsOpen(sign_in_required_ &&
                  IsGitHubLiveFolder(static_cast<LiveFolderProvider>(index)));
  source_.IsEnabled(fields.enabled);
  interval_.IsEnabled(fields.enabled);
  limit_.IsEnabled(fields.enabled);
  Accessible(source_, NativeText(fields.source_label));
}

void LiveFolderEditor::Save() {
  const auto kind = provider_.SelectedIndex();
  const auto minutes = interval_.Value();
  const auto count = limit_.Value();
  auto source = winrt::to_string(source_.Text());
  if (kind == 0)
    source.clear();
  if (kind < 0 || kind > 3 || !std::isfinite(minutes) ||
      !std::isfinite(count) || minutes < 15 || minutes > 480 || count < 1 ||
      count > 100 || std::floor(minutes) != minutes ||
      std::floor(count) != count || (kind != 0 && source.empty()) ||
      !context_.delegate.OnFolderCommand(
          {.kind = FolderCommandKind::kSetLive,
           .folder = folder_,
           .live_kind = static_cast<unsigned>(kind),
           .live_source = source,
           .live_interval_minutes = static_cast<unsigned>(minutes),
           .live_max_items = static_cast<unsigned>(count)})) {
    error_.Text(NativeText(UiText::kLiveFolderInvalid));
    error_.Visibility(xaml::Visibility::Visible);
    return;
  }
  flyout_.Hide();
}
}  // namespace kr::win
