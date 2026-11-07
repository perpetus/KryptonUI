#include "routing_form.hpp"

#include <winrt/Windows.System.h>

#include <algorithm>

namespace kr::win {
namespace {
constexpr auto& kLibrary = design::kTokens.library;
}

RoutingForm::RoutingForm(ViewContext& context) : context_(context) {
  root_.HorizontalScrollBarVisibility(controls::ScrollBarVisibility::Disabled);
  root_.VerticalScrollBarVisibility(controls::ScrollBarVisibility::Auto);
  root_.Margin({kLibrary.header_padding_inline, 0,
                kLibrary.header_padding_inline,
                kLibrary.header_padding_bottom});
  content_.Spacing(kLibrary.row_gap);
  root_.Content(content_);
  title_ = Label(L"", kLibrary.title_size);
  title_.FontWeight(kSemiBold);
  when_ = Label(L"", kLibrary.group_size);
  destination_ = Label(L"", kLibrary.group_size);
  error_ = Label(L"", kLibrary.group_size);
  error_.TextWrapping(xaml::TextWrapping::Wrap);
  content_.Children().Append(title_);
  content_.Children().Append(when_);
  controls::Grid matches;
  matches.ColumnSpacing(kLibrary.tab_gap);
  for (std::size_t i = 0; i < matches_.size(); ++i) {
    AddColumn(matches, 1, xaml::GridUnitType::Star);
    auto button = matches_[i];
    button.MinWidth(0);
    button.FontSize(kLibrary.group_size);
    button.Padding({kLibrary.row_padding_inline, kLibrary.tab_gap,
                    kLibrary.row_padding_inline, kLibrary.tab_gap});
    button.CornerRadius(UniformCorners(kLibrary.tab_radius));
    button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    controls::Grid::SetColumn(button, static_cast<int>(i));
    match_clicks_[i] =
        button.Click(winrt::auto_revoke, [this, i](auto&&, auto&&) {
          draft_.match = static_cast<RoutingMatch>(i);
          for (std::size_t j = 0; j < matches_.size(); ++j)
            matches_[j].IsChecked(j == i);
        });
    matches.Children().Append(button);
  }
  content_.Children().Append(matches);
  pattern_.MaxLength(2048);
  pattern_.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  pattern_.MinWidth(0);
  pattern_.PlaceholderText(L"example.com");
  content_.Children().Append(pattern_);
  content_.Children().Append(destination_);
  workspace_.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  workspace_.MinWidth(0);
  content_.Children().Append(workspace_);
  content_.Children().Append(new_only_);
  content_.Children().Append(enabled_);
  content_.Children().Append(error_);
  controls::StackPanel actions;
  actions.Orientation(controls::Orientation::Horizontal);
  actions.HorizontalAlignment(xaml::HorizontalAlignment::Right);
  actions.Spacing(kLibrary.row_gap);
  actions.Children().Append(cancel_);
  actions.Children().Append(save_);
  content_.Children().Append(actions);
  cancelled_ = cancel_.Click(winrt::auto_revoke, [this](auto&&, auto&&) {
    context_.delegate.OnRoutingCancelled();
  });
  saved_ =
      save_.Click(winrt::auto_revoke, [this](auto&&, auto&&) { Submit(); });
  key_ = pattern_.KeyDown(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::KeyRoutedEventArgs& args) {
        if (!args.Handled() &&
            args.Key() == winrt::Windows::System::VirtualKey::Enter) {
          Submit();
          args.Handled(true);
        }
      });
}

void RoutingForm::Submit() {
  if (!open_)
    return;
  auto value = draft_;
  value.pattern = winrt::to_string(pattern_.Text());
  const auto selected = workspace_.SelectedIndex();
  if (selected < 0 || static_cast<std::size_t>(selected) >= choices_.size())
    return;
  value.workspace = choices_[static_cast<std::size_t>(selected)];
  value.enabled = enabled_.IsChecked().Value();
  value.new_tabs_only = new_only_.IsChecked().Value();
  (void)context_.delegate.OnRoutingSubmitted(value);
}

void RoutingForm::Render(const RoutingEditorState& state,
                         const ShellViewModel& vm) {
  if (!state.open) {
    Close();
    return;
  }
  const bool changed = !open_ || state.draft.serial != draft_.serial;
  open_ = true;
  root_.MaxHeight(vm.library.routing_form_max_height);
  if (changed) {
    workspace_.IsDropDownOpen(false);
    draft_ = state.draft;
    pattern_.Text(winrt::to_hstring(draft_.pattern));
    enabled_.IsChecked(draft_.enabled);
    new_only_.IsChecked(draft_.new_tabs_only);
    workspace_.Items().Clear();
    choices_.clear();
    choices_.push_back({});
    workspace_.Items().Append(
        winrt::box_value(NativeText(UiText::kRoutingRecent, vm.text)));
    int selected = 0;
    for (const auto& space : vm.spaces) {
      if (space.id == draft_.workspace)
        selected = static_cast<int>(choices_.size());
      choices_.push_back(space.id);
      controls::StackPanel row;
      row.Orientation(controls::Orientation::Horizontal);
      row.Spacing(kLibrary.row_gap);
      auto glyph =
          Glyph(winrt::to_hstring(space.glyph), kLibrary.row_icon_size);
      const auto text = glyph.Text();
      const bool icon =
          text.size() == 1 && text[0] >= 0xE000 && text[0] <= 0xF8FF;
      glyph.FontFamily(media::FontFamily(
          winrt::to_hstring(icon ? kTokens.type.icon_family
                                 : kTokens.workspace_picker.emoji_family)));
      glyph.Foreground(context_.brushes.icon);
      row.Children().Append(glyph);
      auto name = Label(winrt::to_hstring(space.name), kTokens.type.size);
      name.Foreground(context_.brushes.text);
      row.Children().Append(name);
      workspace_.Items().Append(row);
    }
    workspace_.SelectedIndex(selected);
  }
  if (changed || vm.changed.Has(Dirty::kText)) {
    title_.Text(NativeText(
        draft_.id ? UiText::kRoutingEditTitle : UiText::kRoutingNewTitle,
        vm.text));
    when_.Text(NativeText(UiText::kRoutingWhen, vm.text));
    destination_.Text(NativeText(UiText::kRoutingDestination, vm.text));
    Accessible(workspace_, destination_.Text());
    Accessible(pattern_, when_.Text());
    constexpr std::array names{UiText::kRoutingDomain, UiText::kRoutingContains,
                               UiText::kRoutingEquals, UiText::kRoutingRegex};
    for (std::size_t i = 0; i < matches_.size(); ++i) {
      matches_[i].Content(winrt::box_value(NativeText(names[i], vm.text)));
      matches_[i].IsChecked(i == std::to_underlying(draft_.match));
    }
    new_only_.Content(
        winrt::box_value(NativeText(UiText::kRoutingNewTabsOnly, vm.text)));
    enabled_.Content(
        winrt::box_value(NativeText(UiText::kRoutingEnabled, vm.text)));
    cancel_.Content(
        winrt::box_value(NativeText(UiText::kRoutingCancel, vm.text)));
    save_.Content(winrt::box_value(NativeText(
        draft_.id ? UiText::kRoutingSave : UiText::kRoutingAdd, vm.text)));
    error_.Text(NativeText(UiText::kRoutingInvalid, vm.text));
  }
  error_.Visibility(state.error ? xaml::Visibility::Visible
                                : xaml::Visibility::Collapsed);
  for (auto label : {title_, when_, destination_, error_})
    label.Foreground(context_.brushes.text);
}

void RoutingForm::Close() noexcept {
  open_ = false;
  try {
    workspace_.IsDropDownOpen(false);
  } catch (...) {
  }
}
}  // namespace kr::win
