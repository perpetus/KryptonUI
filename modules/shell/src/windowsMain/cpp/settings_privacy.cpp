#include "settings_privacy.hpp"

#include <winrt/Microsoft.UI.Xaml.Automation.h>

#include <algorithm>

#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
namespace {
controls::TextBlock Wrapped(bool heading = false) {
  auto label = Label(L"", heading ? kTokens.settings.heading_size
                                  : kTokens.settings.description_size);
  label.FontFamily(
      media::FontFamily(winrt::to_hstring(kTokens.settings.font_family)));
  label.TextWrapping(xaml::TextWrapping::Wrap);
  if (heading)
    label.FontWeight(kSemiBold);
  return label;
}
// The coroutine owns the dialog. The borrowed delegate is used only while the
// settings lifetime token is alive, including during window teardown.
winrt::fire_and_forget Confirm(controls::ContentDialog dialog,
                               std::weak_ptr<int> lifetime,
                               ShellViewDelegate* delegate,
                               PrivacyFeatureId id) {
  bool accepted = false;
  try {
    accepted =
        co_await dialog.ShowAsync() == controls::ContentDialogResult::Primary;
  } catch (const winrt::hresult_error&) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Optional feature confirmation could not open");
  }
  if (!lifetime.expired())
    delegate->OnPrivacyFeatureConfirmed(id, accepted);
}
}  // namespace
PrivacySettings::PrivacySettings(ViewContext& context)
    : PrivacySettings(context,
                      {context.brushes.overlay, context.brushes.transparent,
                       context.brushes.transparent, context.brushes.text,
                       context.brushes.text},
                      false) {}
PrivacySettings::PrivacySettings(ViewContext& context,
                                 const SettingsStyle& style)
    : PrivacySettings(context, style, true) {}
PrivacySettings::PrivacySettings(ViewContext& context,
                                 SettingsStyle style,
                                 bool styled)
    : context_(context), style_(std::move(style)), styled_(styled) {
  root_.Spacing(styled_ ? kTokens.settings.section_gap
                        : kTokens.spacing.section_gap);

  heading_ = Wrapped(true);
  root_.Children().Append(heading_);
  if (!styled_)
    heading_.FontSize(kTokens.type.size_workspace);
  restart_.IsClosable(false);
  restart_.Severity(controls::InfoBarSeverity::Informational);
  restart_.ActionButton(relaunch_);
  root_.Children().Append(restart_);
  error_.IsClosable(false);
  error_.Severity(controls::InfoBarSeverity::Error);
  root_.Children().Append(error_);
  relaunched_ = relaunch_.Click(winrt::auto_revoke, [this](auto&&, auto&&) {
    context_.delegate.OnPrivacyRelaunch();
  });
  for (std::size_t i = 0; i < groups_.size(); ++i) {
    groups_[i].Spacing(styled_ ? kTokens.settings.heading_gap
                               : kTokens.spacing.section_gap);
    headings_[i] = Wrapped(true);
    if (!styled_)
      headings_[i].FontSize(kTokens.type.size_workspace);
    groups_[i].Children().Append(headings_[i]);
    root_.Children().Append(groups_[i]);
  }
}
PrivacySettings::~PrivacySettings() {
  lifetime_.reset();
  if (dialog_)
    dialog_.Hide();
}
void PrivacySettings::Reconcile(const PrivacyFeaturesVm& vm, UiTextView text) {
  for (auto it = rows_.begin(); it != rows_.end();) {
    if (std::ranges::any_of(vm.rows, [&](const auto& row) {
          return row.value->id == it->first;
        })) {
      ++it;
      continue;
    }
    for (auto& group : groups_) {
      std::uint32_t index = 0;
      if (group.Children().IndexOf(it->second->panel, index))
        group.Children().RemoveAt(index);
    }
    it = rows_.erase(it);
  }
  std::array<std::uint32_t, 3> next{1, 1, 1};
  for (const auto& item : vm.rows) {
    const auto& feature = *item.value;
    auto& entry = rows_[feature.id];
    if (!entry) {
      entry = std::make_unique<Row>();
      auto& row = *entry;
      row.panel = style_.Card(row.body);
      if (!styled_)
        row.panel.BorderThickness(UniformThickness(0));
      row.panel.Padding(
          UniformThickness(styled_ ? kTokens.settings.row_inset : 0));
      row.body.Spacing(styled_ ? kTokens.settings.heading_gap
                               : kTokens.spacing.tab_gap);
      row.labels.Spacing(kTokens.settings.label_gap);
      row.main.ColumnSpacing(kTokens.settings.control_gap);
      AddColumn(row.main, 1, xaml::GridUnitType::Star);
      AddColumn(row.main, 0, xaml::GridUnitType::Auto);
      row.title = Wrapped();
      row.title.FontSize(kTokens.settings.body_size);
      row.title.Foreground(style_.text);
      row.details.Spacing(kTokens.spacing.tab_gap);
      row.summary = Wrapped();
      row.risk = Wrapped();
      row.summary.FontSize(kTokens.settings.description_size);
      row.summary.Foreground(style_.secondary);
      row.risk.FontSize(kTokens.settings.description_size);
      row.risk.Foreground(style_.secondary);
      row.labels.Children().Append(row.title);
      row.labels.Children().Append(row.summary);
      row.labels.Children().Append(row.risk);
      if (styled_) {
        row.toggle.MinWidth(0);
        row.toggle.OnContent(winrt::box_value(L""));
        row.toggle.OffContent(winrt::box_value(L""));
      }
      row.toggle.VerticalAlignment(xaml::VerticalAlignment::Center);
      controls::Grid::SetColumn(row.toggle, 1);
      if (styled_) {
        row.main.Children().Append(row.labels);
        row.main.Children().Append(row.toggle);
        row.body.Children().Append(row.main);
      } else {
        row.labels.Children().Clear();
        row.body.Children().Append(row.toggle);
        row.body.Children().Append(row.summary);
        row.body.Children().Append(row.risk);
      }
      row.body.Children().Append(row.expander);
      if (styled_)
        xaml::Automation::AutomationProperties::SetLabeledBy(row.toggle,
                                                             row.title);
      row.expander.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
      row.expander.HorizontalContentAlignment(
          xaml::HorizontalAlignment::Stretch);
      row.expander.Content(row.details);
      for (std::size_t i = 0; i < row.detail_text.size(); ++i) {
        row.detail_headings[i] = Wrapped(true);
        row.detail_text[i] = Wrapped();
        row.details.Children().Append(row.detail_headings[i]);
        row.details.Children().Append(row.detail_text[i]);
      }
      row.details.Children().Append(row.manage);
      row.toggled = row.toggle.Toggled(
          winrt::auto_revoke, [this, id = feature.id](auto&& sender, auto&&) {
            if (!reconciling_)
              context_.delegate.OnPrivacyFeatureChanged(
                  id, sender.template as<controls::ToggleSwitch>().IsOn());
          });
      row.clicked = row.manage.Click(
          winrt::auto_revoke, [this, id = feature.id](auto&&, auto&&) {
            context_.delegate.OnPrivacyFeatureManage(id);
          });
    }
    auto& row = *entry;
    const auto group_index = static_cast<std::size_t>(feature.group);
    auto children = groups_[group_index].Children();
    const auto target = next[group_index]++;
    std::uint32_t index = 0;
    if (!children.IndexOf(row.panel, index) || index != target) {
      for (auto& group : groups_) {
        if (group.Children().IndexOf(row.panel, index))
          group.Children().RemoveAt(index);
      }
      children.InsertAt(target, row.panel);
    }
    row.title.Text(winrt::to_hstring(feature.title));
    if (!styled_)
      row.toggle.Header(winrt::box_value(row.title.Text()));
    row.toggle.IsOn(feature.flags.Has(PrivacyFeatureFlag::kEnabled));
    row.toggle.IsEnabled(item.editable);
    row.summary.Text(winrt::to_hstring(feature.summary));
    row.risk.Text(NativeText(item.risk_caption, text));
    row.expander.Header(
        winrt::box_value(NativeText(UiText::kPrivacyDetails, text)));
    constexpr std::array keys{UiText::kPrivacyShared, UiText::kPrivacyRisk,
                              UiText::kPrivacyOff};
    const std::array<std::string_view, 3> values{
        feature.data_shared, feature.risk_detail, feature.when_off};
    for (std::size_t i = 0; i < keys.size(); ++i) {
      row.detail_headings[i].Text(NativeText(keys[i], text));
      row.detail_text[i].Text(winrt::to_hstring(values[i]));
    }
    row.manage.Content(
        winrt::box_value(NativeText(UiText::kPrivacyManage, text)));
    row.manage.Visibility(feature.manage_url.empty()
                              ? xaml::Visibility::Collapsed
                              : xaml::Visibility::Visible);
    xaml::Automation::AutomationProperties::SetHelpText(
        row.toggle, winrt::to_hstring(feature.summary));
  }
  for (std::size_t i = 0; i < groups_.size(); ++i) {
    headings_[i].Text(NativeText(
        PrivacyGroupText(static_cast<PrivacyFeatureGroup>(i)), text));
    groups_[i].Visibility(next[i] > 1 ? xaml::Visibility::Visible
                                      : xaml::Visibility::Collapsed);
  }
}
void PrivacySettings::Render(const PrivacyFeaturesVm& vm,
                             UiTextView text,
                             bool refresh_text) {
  const bool refresh = !text_ready_ || refresh_text || revision_ != vm.revision;
  root_.Visibility(vm.visible && !vm.rows.empty()
                       ? xaml::Visibility::Visible
                       : xaml::Visibility::Collapsed);
  if (!vm.visible || !vm.confirmation) {
    if (dialog_) {
      dialog_.Hide();
      dialog_ = nullptr;
    }
    confirmation_.reset();
  }
  if (!vm.visible)
    return;
  if (refresh) {
    reconciling_ = true;
    Reconcile(vm, text);
    reconciling_ = false;
    heading_.Text(NativeText(UiText::kPrivacyOptionalFeatures, text));
    restart_.Title(NativeText(UiText::kPrivacyRestart, text));
    restart_.IsOpen(vm.restart_pending);
    if (styled_)
      restart_.Visibility(vm.restart_pending ? xaml::Visibility::Visible
                                             : xaml::Visibility::Collapsed);
    relaunch_.Content(
        winrt::box_value(NativeText(UiText::kPrivacyRelaunch, text)));
    relaunch_.IsEnabled(vm.can_relaunch);
    error_.Title(NativeText(UiText::kPrivacyError, text));
    error_.IsOpen(vm.failed);
    if (styled_)
      error_.Visibility(vm.failed ? xaml::Visibility::Visible
                                  : xaml::Visibility::Collapsed);
    revision_ = vm.revision;
    text_ready_ = true;
  }
  if (vm.confirmation && confirmation_ != vm.confirmation->id) {
    const auto& feature = *vm.confirmation;
    dialog_ = controls::ContentDialog();
    dialog_.XamlRoot(root_.XamlRoot());
    dialog_.RequestedTheme(root_.ActualTheme());
    dialog_.FlowDirection(root_.FlowDirection());
    dialog_.Title(winrt::box_value(NativeText(UiText::kPrivacyConfirm, text)));
    controls::StackPanel content;
    content.Spacing(kTokens.spacing.section_gap);
    auto title = Wrapped(true);
    title.Text(winrt::to_hstring(feature.title));
    content.Children().Append(title);
    for (const auto value : {std::string_view(feature.data_shared),
                             std::string_view(feature.risk_detail)}) {
      auto label = Wrapped();
      label.Text(winrt::to_hstring(value));
      content.Children().Append(label);
    }
    dialog_.Content(content);
    dialog_.PrimaryButtonText(NativeText(UiText::kPrivacyEnable, text));
    dialog_.CloseButtonText(NativeText(UiText::kPrivacyCancel, text));
    dialog_.DefaultButton(controls::ContentDialogButton::Close);
    confirmation_ = feature.id;
    Confirm(dialog_, lifetime_, &context_.delegate, feature.id);
  }
}
}  // namespace kr::win
