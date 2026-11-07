#include "permission_prompt.hpp"

#include <winrt/Windows.System.h>

#include <algorithm>
#include <limits>

#include "browser_tools_icons.hpp"
#include "krypton/ui/layout/popup_layout.hpp"
#include "krypton/ui/platform/platform.hpp"
#include "native_input.hpp"

namespace kr::win {
using namespace controls;
PermissionPromptView::PermissionPromptView(HWND parent, ViewContext& context)
    : context_(context), island_(parent) {}
PermissionPromptView::~PermissionPromptView() {
  lifetime_.reset();
  closed_.revoke();
  opened_.revoke();
  loaded_.revoke();
  sized_.revoke();
  key_.revoke();
  fallback_key_.revoke();
  Close();
}
void PermissionPromptView::Build() {
  if (built_)
    return;
  const auto& t = kTokens.browser_tools;
  const auto accent_key =
      winrt::box_value(winrt::hstring(L"AccentButtonStyle"));
  const auto resources = xaml::Application::Current().Resources();
  if (resources.HasKey(accent_key))
    accent_ = resources.Lookup(accent_key).as<xaml::Style>();
  content_.RowSpacing(t.gap);
  for (int row = 0; row < 5; ++row)
    AddRow(content_, 0, xaml::GridUnitType::Auto);
  Grid header;
  AddColumn(header, 1, xaml::GridUnitType::Star);
  AddColumn(header, 0, xaml::GridUnitType::Auto);
  StackPanel heading;
  origin_ = Label(L"", kTokens.type.size);
  origin_.FontWeight(kSemiBold);
  origin_.TextWrapping(xaml::TextWrapping::Wrap);
  title_ =
      Label(winrt::to_hstring(kPermissionPromptTitle), kTokens.type.size_small);
  heading.Children().Append(origin_);
  heading.Children().Append(title_);
  header.Children().Append(heading);
  close_ = IconButton(context_.brushes, glyphs::kClose,
                      winrt::to_hstring(kClosePermissionPromptLabel),
                      [this] { Decide(PermissionDecision::kDismiss); });
  Grid::SetColumn(close_->control, 1);
  header.Children().Append(close_->control);
  content_.Children().Append(header);
  embedding_ = Label(L"", kTokens.type.size_small);
  embedding_.TextWrapping(xaml::TextWrapping::Wrap);
  auto embedding_caption =
      Label(winrt::to_hstring(kEmbeddedOriginLabel), kTokens.type.size_small);
  embedding_caption.Foreground(context_.brushes.muted);
  embedding_section_.Children().Append(embedding_caption);
  embedding_section_.Children().Append(embedding_);
  Grid::SetRow(embedding_section_, 1);
  content_.Children().Append(embedding_section_);
  requests_.Spacing(t.gap);
  Grid::SetRow(requests_, 2);
  content_.Children().Append(requests_);
  message_ = Label(L"", kTokens.type.size_small);
  message_.TextWrapping(xaml::TextWrapping::Wrap);
  Grid::SetRow(message_, 3);
  content_.Children().Append(message_);
  actions_.Spacing(t.gap);
  const auto action = [&](std::string_view label, PermissionDecision decision) {
    auto button = IconButton(context_.brushes, L"", winrt::to_hstring(label),
                             [this, decision] { Decide(decision); });
    button->control.Content(winrt::box_value(winrt::to_hstring(label)));
    button->control.Width(std::numeric_limits<double>::quiet_NaN());
    button->control.Height(std::numeric_limits<double>::quiet_NaN());
    button->control.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    button->control.Padding(
        UniformThickness(kTokens.app_menu.control_padding_block));
    actions_.Children().Append(button->control);
    return button;
  };
  allow_once_ =
      action(kPermissionAllowOnceLabel, PermissionDecision::kAllowOnce);
  allow_always_ =
      action(kPermissionAllowAlwaysLabel, PermissionDecision::kAllowAlways);
  block_ = action(kPermissionBlockLabel, PermissionDecision::kBlock);
  Grid::SetRow(actions_, 4);
  content_.Children().Append(actions_);
  scroll_.Content(content_);
  scroll_.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
  scroll_.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroll_.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  surface_.Content(scroll_);
  surface_.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
  surface_.MinWidth(0);
  surface_.MinHeight(0);
  surface_.Padding(UniformThickness(t.prompt_padding));
  surface_.CornerRadius(UniformCorners(t.prompt_radius));
  surface_.BorderThickness(UniformThickness(kTokens.menu.border_width));
  surface_.BorderBrush(context_.brushes.menu_border);
  backdrop_.CornerRadius(surface_.CornerRadius());
  backdrop_.IsHitTestVisible(false);
  layers_.Children().Append(backdrop_);
  layers_.Children().Append(surface_);
  popup_.Child(layers_);
  popup_.IsLightDismissEnabled(true);
  popup_.ShouldConstrainToRootBounds(true);
  const std::weak_ptr<int> weak = lifetime_;
  closed_ = popup_.Closed(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
    if (!weak.expired() && open_ && !popup_.IsOpen()) {
      open_ = false;
      island_.Hide();
      context_.delegate.OnPermissionPromptDecision(
          request_, PermissionDecision::kDismiss);
    }
  });
  opened_ = popup_.Opened(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
    if (!weak.expired() && open_) {
      island_.Raise();
      block_->control.Focus(xaml::FocusState::Programmatic);
    }
  });
  key_ = content_.KeyDown(
      winrt::auto_revoke,
      [this, weak](auto&&, const xaml::Input::KeyRoutedEventArgs& args) {
        if (!weak.expired() &&
            args.Key() == winrt::Windows::System::VirtualKey::Escape) {
          Decide(PermissionDecision::kDismiss);
          args.Handled(true);
        }
      });
  fallback_key_ = content_.ProcessKeyboardAccelerators(
      winrt::auto_revoke,
      [this, weak](
          auto&&,
          const xaml::Input::ProcessKeyboardAcceleratorEventArgs& args) {
        if (!weak.expired() && !args.Handled() &&
            context_.input.ForwardUnhandledKey(
                static_cast<std::uint32_t>(args.Key())))
          args.Handled(true);
      });
  island_.Ensure();
  context_.brushes.Install(island_.Root());
  island_.Root().Children().Append(popup_);
  loaded_ =
      island_.Root().Loaded(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
        if (!weak.expired())
          PositionAndOpen();
      });
  sized_ =
      surface_.SizeChanged(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
        if (!weak.expired())
          PositionAndOpen();
      });
  built_ = true;
}
void PermissionPromptView::Close() noexcept {
  open_ = false;
  try {
    popup_.IsOpen(false);
    island_.Hide();
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Permission prompt could not close");
  }
}
void PermissionPromptView::Decide(PermissionDecision value) {
  if (!open_)
    return;
  const auto request = request_;
  Close();
  context_.delegate.OnPermissionPromptDecision(request, value);
}
void PermissionPromptView::Render(const ShellViewModel& vm) {
  const auto* prompt = vm.browser_tools.prompt;
  if (!prompt) {
    if (open_)
      Close();
    return;
  }
  if (open_ && request_ != prompt->id)
    Close();
  Build();
  const bool changed = revision_ != vm.browser_tools.prompt_revision;
  const auto theme =
      vm.theme.dark ? xaml::ElementTheme::Dark : xaml::ElementTheme::Light;
  layers_.RequestedTheme(theme);
  surface_.RequestedTheme(theme);
  backdrop_.RequestedTheme(theme);
  content_.FlowDirection(vm.text.right_to_left
                             ? xaml::FlowDirection::RightToLeft
                             : xaml::FlowDirection::LeftToRight);
  surface_.Background(context_.brushes.menu_background);
  backdrop_.SystemBackdrop(
      material_.For(context_.brushes.menu_backdrop_enabled(),
                    context_.brushes.menu_material()));
  origin_.Foreground(context_.brushes.text);
  title_.Foreground(context_.brushes.muted);
  embedding_.Foreground(context_.brushes.muted);
  message_.Foreground(context_.brushes.muted);
  for (const auto* action :
       {allow_once_.get(), allow_always_.get(), block_.get()}) {
    action->control.Style(nullptr);
    action->control.Background(context_.brushes.pill);
    action->control.Foreground(context_.brushes.text);
    action->control.BorderThickness(
        UniformThickness(kTokens.menu.border_width));
    action->control.BorderBrush(context_.brushes.menu_border);
  }
  if (accent_) {
    const auto primary =
        prompt->allow_once ? allow_once_->control : allow_always_->control;
    primary.Style(accent_);
    primary.ClearValue(controls::Control::BackgroundProperty());
    primary.ClearValue(controls::Control::ForegroundProperty());
  }
  if (changed) {
    request_ = prompt->id;
    revision_ = vm.browser_tools.prompt_revision;
    origin_.Text(winrt::to_hstring(prompt->origin));
    embedding_.Text(winrt::to_hstring(prompt->embedding_origin));
    embedding_section_.Visibility(!prompt->embedding_origin.empty() &&
                                          prompt->embedding_origin !=
                                              prompt->origin
                                      ? xaml::Visibility::Visible
                                      : xaml::Visibility::Collapsed);
    message_.Text(winrt::to_hstring(prompt->message));
    message_.Visibility(prompt->message.empty() ? xaml::Visibility::Collapsed
                                                : xaml::Visibility::Visible);
    for (std::size_t i = request_rows_.size(); i < prompt->requests.size();
         ++i) {
      Grid row;
      AddColumn(row, kTokens.browser_tools.permission_icon_width,
                xaml::GridUnitType::Pixel);
      AddColumn(row, 1, xaml::GridUnitType::Star);
      auto icon = Glyph(L"", kTokens.browser_tools.prompt_icon_size);
      auto label = Label(L"", kTokens.type.size);
      label.TextWrapping(xaml::TextWrapping::Wrap);
      Grid::SetColumn(label, 1);
      row.Children().Append(icon);
      row.Children().Append(label);
      request_rows_.push_back(row);
      request_icons_.push_back(icon);
      request_labels_.push_back(label);
      requests_.Children().Append(row);
    }
    for (std::size_t i = 0; i < request_rows_.size(); ++i) {
      const bool visible = i < prompt->requests.size();
      request_rows_[i].Visibility(visible ? xaml::Visibility::Visible
                                          : xaml::Visibility::Collapsed);
      if (visible) {
        request_icons_[i].Text(
            winrt::hstring(PermissionIcon(prompt->requests[i].kind)));
        request_labels_[i].Text(winrt::to_hstring(prompt->requests[i].label));
      }
    }
    allow_once_->control.Visibility(prompt->allow_once
                                        ? xaml::Visibility::Visible
                                        : xaml::Visibility::Collapsed);
    allow_always_->control.Visibility(prompt->allow_always
                                          ? xaml::Visibility::Visible
                                          : xaml::Visibility::Collapsed);
  }
  for (auto& icon : request_icons_)
    icon.Foreground(context_.brushes.icon);
  for (auto& label : request_labels_)
    label.Foreground(context_.brushes.text);
  anchor_ = prompt->anchor;
  client_ = vm.client;
  open_ = true;
  island_.Show(vm.client, vm.scale_factor, theme);
  PositionAndOpen();
}
void PermissionPromptView::PositionAndOpen() noexcept {
  if (!open_ || positioning_ || !island_.IsReady())
    return;
  positioning_ = true;
  struct Reset {
    bool& value;
    ~Reset() { value = false; }
  } reset{positioning_};
  try {
    const auto available = PopupViewport(client_);
    const auto inset =
        2 * (kTokens.browser_tools.prompt_padding + kTokens.menu.border_width);
    surface_.Width(
        std::min(kTokens.browser_tools.prompt_width, available.width));
    surface_.MaxHeight(available.height);
    const auto inner_width = std::max(0.0, surface_.Width() - inset);
    scroll_.Width(inner_width);
    scroll_.MaxWidth(inner_width);
    content_.MaxWidth(inner_width);
    scroll_.MaxHeight(std::max(0.0f, available.height - inset));
    surface_.Measure({available.width, available.height});
    const auto size = surface_.DesiredSize();
    const auto frame = PlaceAnchoredPopup(
        client_, anchor_, {size.Width, size.Height},
        content_.FlowDirection() == xaml::FlowDirection::RightToLeft);
    if (frame.empty()) {
      Close();
      return;
    }
    popup_.HorizontalOffset(frame.x - client_.x);
    popup_.VerticalOffset(frame.y - client_.y);
    if (!popup_.IsOpen())
      popup_.IsOpen(true);
    island_.Raise();
  } catch (...) {
    Close();
    platform::WriteDefaultLog(LogLevel::kError,
                              "Permission prompt could not open");
  }
}
}  // namespace kr::win
