#include "command_bar.hpp"

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <algorithm>

#include "krypton/ui/layout/popup_layout.hpp"
#include "krypton/ui/layout/search_box_layout.hpp"
#include "krypton/ui/model/address_key.hpp"
#include "krypton/ui/platform/platform.hpp"
#include "native_input.hpp"

namespace kr::win {
using namespace controls;
using winrt::Windows::System::VirtualKey;
using xaml::Thickness;
using xaml::Visibility;

CommandBar::~CommandBar() {
  lifetime_.reset();
  command_text_.revoke();
  command_selection_.revoke();
  composition_started_.revoke();
  composition_changed_.revoke();
  composition_ended_.revoke();
  focused_.revoke();
  blurred_.revoke();
  owner_loaded_.revoke();
  owner_size_.revoke();
  input_loaded_.revoke();
  popup_opened_.revoke();
  surface_size_.revoke();
  input_size_.revoke();
  text_scale_changed_.revoke();
  popup_closed_.revoke();
  fallback_key_.revoke();
  command_key_.revoke();
  Close();
}

void CommandBar::Close() noexcept {
  edit_pending_ = false;
  composing_ = false;
  fade_.Stop();
  requested_open_ = false;
  focus_pending_ = false;
  if (!command_popup_) {
    island_.Hide();
    return;
  }
  const bool previous = std::exchange(rendering_, true);
  try {
    context_.glass.Place(surface_, {});
    command_popup_.IsOpen(false);
    if (suggestions_)
      suggestions_->ClearImages();
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Could not close the search box");
  }
  rendering_ = previous;
  island_.Hide();
}

void CommandBar::QueueEdit() {
  if (rendering_ || !requested_open_ || edit_pending_)
    return;
  edit_pending_ = true;
  const auto serial = command_serial_;
  const std::weak_ptr<int> weak = lifetime_;
  command_input_.DispatcherQueue().TryEnqueue([this, weak, serial] {
    if (weak.expired() || serial != command_serial_ || !edit_pending_)
      return;
    edit_pending_ = false;
    ReportEdit();
  });
}

void CommandBar::ReportEdit() {
  if (rendering_ || !requested_open_)
    return;
  const auto start =
      static_cast<std::uint32_t>(command_input_.SelectionStart());
  const auto end =
      start + static_cast<std::uint32_t>(command_input_.SelectionLength());
  context_.delegate.OnAddressEditChanged(
      winrt::to_string(command_input_.Text()), {start, end, composing_});
}

void CommandBar::SetFocusBlocked(bool blocked) noexcept {
  focus_blocked_ = blocked;
  if (!blocked)
    OpenWhenLoaded();
}

void CommandBar::SetText(UiTextView text) {
  direction_ = text.right_to_left ? xaml::FlowDirection::RightToLeft
                                  : xaml::FlowDirection::LeftToRight;
  placeholder_ = NativeText(UiText::kAddress, text);
  if (command_input_) {
    surface_.FlowDirection(direction_);
    command_input_.PlaceholderText(placeholder_);
    Accessible(command_input_, placeholder_);
  }
}

void CommandBar::Render(const CommandBarVm& vm,
                        Rect client,
                        float scale,
                        xaml::ElementTheme theme,
                        const ImageStore* images) {
  RenderCommandBar(vm, client, scale, theme, images);
}

void CommandBar::EnsureCommandBar() {
  if (command_popup_)
    return;
  island_.Ensure();
  const auto& root = island_.Root();
  context_.brushes.Install(root);
  const auto popup = controls::Primitives::Popup();
  text_settings_ = winrt::Windows::UI::ViewManagement::UISettings();
  popup.IsLightDismissEnabled(true);
  // Native menu backdrops require a windowed popup. Shared layout still bounds
  // both attached and floating editors to the browser's popup viewport.
  popup.ShouldConstrainToRootBounds(false);
  surface_ = Border();
  surface_.FlowDirection(direction_);
  surface_.Background(context_.brushes.search_background);
  surface_.BorderBrush(context_.brushes.search_border);
  surface_.BorderThickness(UniformThickness(kTokens.overlay.border_width));
  surface_.CornerRadius(UniformCorners(kTokens.radius.overlay));
  Grid content;
  AddRow(content, 0, xaml::GridUnitType::Auto);
  AddRow(content, 1, xaml::GridUnitType::Star);
  input_row_ = Grid();
  input_row_.MinHeight(kTokens.overlay.input_height);
  input_row_.Padding(UniformThickness(kTokens.overlay.input_padding));
  input_row_.Margin(
      {kTokens.overlay.input_margin, 0, kTokens.overlay.input_margin, 0});
  AddColumn(input_row_, 0, xaml::GridUnitType::Auto);
  AddColumn(input_row_, 1, xaml::GridUnitType::Star);
  const auto search = Glyph(glyphs::kSearch);
  search.Foreground(context_.brushes.search_text);
  search.Margin({0, 0, kTokens.overlay.icon_gap, 0});
  input_row_.Children().Append(search);
  command_input_ = TextBox();
  command_input_.PlaceholderText(placeholder_);
  command_input_.MinHeight(0);
  command_input_.MinWidth(0);
  command_input_.BorderThickness(UniformThickness(0));
  command_input_.Padding(UniformThickness(0));
  command_input_.Background(context_.brushes.transparent);
  command_input_.Foreground(context_.brushes.search_input);
  command_input_.PlaceholderForeground(context_.brushes.search_description);
  command_input_.VerticalAlignment(xaml::VerticalAlignment::Center);
  command_input_.IsSpellCheckEnabled(false);
  command_input_.IsTextPredictionEnabled(false);
  command_input_.FontFamily(
      media::FontFamily(winrt::to_hstring(kTokens.type.family)));
  command_input_.FontSize(kTokens.type.size * kTokens.overlay.input_font_scale);
  command_input_.FontWeight(kMedium);
  const auto set = [&](std::wstring_view key, const auto& brush) {
    command_input_.Resources().Insert(winrt::box_value(winrt::hstring(key)),
                                      brush);
  };
  for (const auto key :
       {L"TextControlBackground", L"TextControlBackgroundPointerOver",
        L"TextControlBackgroundFocused", L"TextControlBorderBrush",
        L"TextControlBorderBrushPointerOver", L"TextControlBorderBrushFocused"})
    set(key, context_.brushes.transparent);
  for (const auto key :
       {L"TextControlForeground", L"TextControlForegroundPointerOver",
        L"TextControlForegroundFocused", L"TextBoxForegroundThemeBrush",
        L"TextFillColorPrimaryBrush"})
    set(key, context_.brushes.search_input);
  Accessible(command_input_, placeholder_);
  Grid::SetColumn(command_input_, 1);
  input_row_.Children().Append(command_input_);
  input_scroll_ = ScrollViewer();
  input_scroll_.MinHeight(0);
  input_scroll_.MinWidth(0);
  input_scroll_.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  input_scroll_.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  input_scroll_.Content(input_row_);
  content.Children().Append(input_scroll_);
  suggestions_ = std::make_unique<SuggestionList>(context_);
  suggestions_->Root().Margin({kTokens.overlay.results_padding, 0,
                               kTokens.overlay.results_padding,
                               kTokens.overlay.results_bottom_margin});
  suggestions_->Root().MinWidth(0);
  suggestions_->Root().MinHeight(0);
  suggestions_->Root().MaxHeight(kTokens.overlay.results_max_height);
  Grid::SetRow(suggestions_->Root(), 1);
  content.Children().Append(suggestions_->Root());
  surface_.Child(content);
  focused_ = surface_.GotFocus(winrt::auto_revoke, [this](auto&&, auto&&) {
    if (requested_open_)
      context_.delegate.OnAddressFocusChanged(true);
  });
  blurred_ = surface_.LostFocus(winrt::auto_revoke, [this](auto&&, auto&&) {
    const auto root = surface_.XamlRoot();
    auto element = root ? xaml::Input::FocusManager::GetFocusedElement(root)
                              .try_as<xaml::DependencyObject>()
                        : xaml::DependencyObject{nullptr};
    bool inside = false;
    while (element) {
      if (element == surface_) {
        inside = true;
        break;
      }
      element = media::VisualTreeHelper::GetParent(element);
    }
    context_.delegate.OnAddressFocusChanged(requested_open_ && inside);
  });
  popup.Child(surface_);
  surface_size_ = surface_.SizeChanged(
      winrt::auto_revoke, [this](auto&&, auto&&) { Reposition(); });
  input_size_ = input_row_.SizeChanged(
      winrt::auto_revoke, [this](auto&&, auto&&) { Reposition(); });
  command_text_ = command_input_.TextChanged(
      winrt::auto_revoke, [this](auto&&, auto&&) { QueueEdit(); });
  command_selection_ = command_input_.SelectionChanged(
      winrt::auto_revoke, [this](auto&&, auto&&) { QueueEdit(); });
  composition_started_ = command_input_.TextCompositionStarted(
      winrt::auto_revoke, [this](auto&&, auto&&) {
        composing_ = true;
        QueueEdit();
      });
  composition_changed_ = command_input_.TextCompositionChanged(
      winrt::auto_revoke, [this](auto&&, auto&&) { QueueEdit(); });
  composition_ended_ = command_input_.TextCompositionEnded(
      winrt::auto_revoke, [this](auto&&, auto&&) {
        composing_ = false;
        QueueEdit();
      });
  command_key_ = command_input_.PreviewKeyDown(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::KeyRoutedEventArgs& args) {
        if (composing_)
          return;
        if (edit_pending_) {
          edit_pending_ = false;
          ReportEdit();
        }
        const auto down = [](VirtualKey key) {
          return (winrt::Microsoft::UI::Input::InputKeyboardSource::
                      GetKeyStateForCurrentThread(key) &
                  winrt::Windows::UI::Core::CoreVirtualKeyStates::Down) !=
                 winrt::Windows::UI::Core::CoreVirtualKeyStates::None;
        };
        const bool shift = down(VirtualKey::Shift),
                   control = down(VirtualKey::Control);
        Flags<AddressModifier> modifiers;
        modifiers.Set(AddressModifier::kShift, shift)
            .Set(AddressModifier::kControl, control);
        const auto address_key = [&](AddressKey key) {
          if (down(VirtualKey::Menu))
            return false;
          const auto route = RouteAddressKey(key, modifiers);
          if (route == AddressKeyRoute::kBrowser &&
              context_.input.ForwardUnhandledKey(
                  static_cast<std::uint32_t>(args.Key())))
            args.Handled(true);
          return route == AddressKeyRoute::kAddress;
        };
        if (args.Key() == VirtualKey::Enter) {
          context_.delegate.OnAddressKey(AddressKey::kControl, modifiers);
          context_.delegate.OnCommandAccepted(
              down(VirtualKey::Menu) || (control && shift)
                  ? Disposition::kNewTab
              : shift ? Disposition::kNewWindow
                      : Disposition::kCurrentTab);
        } else if (args.Key() == VirtualKey::Escape) {
          if (!context_.delegate.OnAddressKey(AddressKey::kEscape, modifiers))
            context_.delegate.OnCommandBarDismissed();
        } else if (args.Key() == VirtualKey::Down ||
                   args.Key() == VirtualKey::Up) {
          if (!address_key(args.Key() == VirtualKey::Down ? AddressKey::kDown
                                                          : AddressKey::kUp))
            return;
          if (!context_.delegate.OnAddressKey(args.Key() == VirtualKey::Down
                                                  ? AddressKey::kDown
                                                  : AddressKey::kUp,
                                              modifiers))
            context_.delegate.OnCommandSelectionMoved(
                args.Key() == VirtualKey::Down ? 1 : -1);
        } else if (args.Key() == VirtualKey::PageDown ||
                   args.Key() == VirtualKey::PageUp) {
          if (!address_key(args.Key() == VirtualKey::PageDown
                               ? AddressKey::kPageDown
                               : AddressKey::kPageUp))
            return;
          if (!context_.delegate.OnAddressKey(args.Key() == VirtualKey::PageDown
                                                  ? AddressKey::kPageDown
                                                  : AddressKey::kPageUp,
                                              modifiers))
            return;
        } else if (args.Key() == VirtualKey::Tab) {
          if (!address_key(AddressKey::kTab))
            return;
          if (!context_.delegate.OnAddressKey(AddressKey::kTab, modifiers))
            return;
        } else if (args.Key() == VirtualKey::Delete && shift) {
          if (!address_key(AddressKey::kDeleteMatch))
            return;
          if (!context_.delegate.OnAddressKey(AddressKey::kDeleteMatch,
                                              modifiers))
            return;
        } else
          return;
        args.Handled(true);
      });
  fallback_key_ = surface_.ProcessKeyboardAccelerators(
      winrt::auto_revoke,
      [this](auto&&,
             const xaml::Input::ProcessKeyboardAcceleratorEventArgs& args) {
        if (!args.Handled() && context_.input.ForwardUnhandledKey(
                                   static_cast<std::uint32_t>(args.Key())))
          args.Handled(true);
      });
  popup_closed_ = popup.Closed(winrt::auto_revoke, [this](auto&&, auto&&) {
    if (!rendering_ && requested_open_ && !command_popup_.IsOpen()) {
      requested_open_ = false;
      focus_pending_ = false;
      fade_.Stop();
      island_.Hide();
      context_.delegate.OnCommandBarDismissed();
      context_.hover_changed();
    }
  });
  const std::weak_ptr<int> weak = lifetime_;
  const auto queue = root.DispatcherQueue();
  text_scale_changed_ = text_settings_.TextScaleFactorChanged(
      winrt::auto_revoke, [this, weak, queue](auto&&, auto&&) {
        queue.TryEnqueue([this, weak] {
          if (!weak.expired())
            Reposition();
        });
      });
  popup_opened_ =
      popup.Opened(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
        if (!weak.expired()) {
          FocusInput();
          fade_.Play(surface_.Child(), context_.motion,
                     context_.brushes.high_contrast());
        }
      });
  owner_loaded_ = root.Loaded(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
    if (!weak.expired())
      OpenWhenLoaded();
  });
  input_loaded_ =
      command_input_.Loaded(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
        if (!weak.expired())
          FocusInput();
      });
  owner_size_ =
      root.SizeChanged(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
        if (!weak.expired())
          OpenWhenLoaded();
      });
  // Inherit one XamlRoot through the visual tree. Popup content must not move
  // between the independent chrome islands.
  owner_ = root;
  root.Children().Append(popup);
  command_popup_ = popup;
}

void CommandBar::OpenWhenLoaded() noexcept {
  if (!requested_open_ || focus_blocked_ || !command_popup_)
    return;
  try {
    if (!island_.IsReady())
      return;
    Reposition();
    island_.Raise();
    if (!command_popup_.IsOpen())
      command_popup_.IsOpen(true);
    FocusInput();
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Could not open the search box");
  }
}

void CommandBar::FocusInput() noexcept {
  if (!requested_open_ || focus_blocked_ || !focus_pending_ ||
      !command_popup_.IsOpen() || !command_input_.IsLoaded())
    return;
  try {
    if (!command_input_.Focus(xaml::FocusState::Programmatic))
      return;
    // Focus() also succeeds when this same input already owns focus. A new
    // address session still needs its actual focus reported in that case.
    context_.delegate.OnAddressFocusChanged(true);
    command_input_.Select(static_cast<std::int32_t>(selection_pending_.start),
                          static_cast<std::int32_t>(selection_pending_.end -
                                                    selection_pending_.start));
    focus_pending_ = false;
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Could not focus the search box");
  }
}

void CommandBar::SetAddressAnchor(Rect anchor) noexcept {
  if (address_anchor_ == anchor)
    return;
  address_anchor_ = anchor;
  Reposition();
}

void CommandBar::Reposition() noexcept {
  if (positioning_ || !requested_open_ || !command_popup_ || !surface_ ||
      client_.empty())
    return;
  positioning_ = true;
  struct PositionScope {
    bool& active;
    ~PositionScope() { active = false; }
  } scope{positioning_};
  try {
    const Rect client = island_.ClientBounds();
    if (client.empty())
      return;
    const bool attached = !floating_ && !address_anchor_.empty();
    const float radius =
        attached ? kTokens.radius.pill : kTokens.radius.overlay;
    const auto background = context_.glass.For(
        surface_, menu_backdrop_ ? GlassRole::kMenu : GlassRole::kSearch,
        menu_backdrop_ ? context_.brushes.menu_background
                       : context_.brushes.search_background,
        radius);
    if (surface_.Background() != background)
      surface_.Background(background);
    const auto corners = UniformCorners(radius);
    if (surface_.CornerRadius() != corners)
      surface_.CornerRadius(corners);
    const auto border = menu_backdrop_ ? context_.brushes.menu_border
                                       : context_.brushes.search_border;
    if (surface_.BorderBrush() != border)
      surface_.BorderBrush(border);
    surface_.BorderThickness(UniformThickness(kTokens.overlay.border_width));
    const auto backdrop = material_.For(
        menu_backdrop_ && context_.brushes.menu_backdrop_enabled(),
        context_.brushes.menu_material());
    if (command_popup_.SystemBackdrop() != backdrop)
      command_popup_.SystemBackdrop(backdrop);
    const float input_height =
        attached ? std::max(0.0f, address_anchor_.height -
                                      2 * kTokens.overlay.border_width)
                 : kTokens.overlay.input_height;
    if (input_row_.MinHeight() != input_height)
      input_row_.MinHeight(input_height);
    const auto padding = attached
                             ? Thickness{kTokens.spacing.field_padding, 0,
                                         kTokens.spacing.field_padding, 0}
                             : UniformThickness(kTokens.overlay.input_padding);
    if (input_row_.Padding() != padding)
      input_row_.Padding(padding);
    const float margin = attached ? 0 : kTokens.overlay.input_margin;
    const Thickness input_margin{margin, 0, margin, 0};
    if (input_row_.Margin() != input_margin)
      input_row_.Margin(input_margin);
    const double input_font =
        kTokens.type.size * (attached ? 1 : kTokens.overlay.input_font_scale);
    if (command_input_.FontSize() != input_font)
      command_input_.FontSize(input_font);
    const float factor = static_cast<float>(text_settings_.TextScaleFactor());
    const auto bounds =
        attached ? ComputeAttachedSearchBoxBounds(
                       client, address_anchor_,
                       static_cast<float>(surface_.ActualHeight()))
                 : ComputeSearchBoxBounds(
                       client, static_cast<float>(surface_.ActualHeight()),
                       kTokens.type.size * factor);
    context_.glass.Place(surface_, bounds);
    if (surface_.Width() != bounds.width)
      surface_.Width(bounds.width);
    const float available =
        attached ? std::max(0.0f, PopupViewport(client).bottom() - bounds.y)
                 : PopupViewport(client).height;
    if (surface_.MaxHeight() != available)
      surface_.MaxHeight(available);
    const float input_viewport =
        std::max(0.0f, available - 2 * kTokens.overlay.border_width);
    if (input_scroll_.MaxHeight() != input_viewport)
      input_scroll_.MaxHeight(input_viewport);
    const float results_viewport = ComputeSearchResultsMaxHeight(
        client, static_cast<float>(input_row_.ActualHeight()));
    const float bounded_results =
        attached
            ? std::min(
                  results_viewport,
                  std::max(0.0f,
                           available -
                               static_cast<float>(input_row_.ActualHeight()) -
                               2 * kTokens.overlay.border_width -
                               kTokens.overlay.results_bottom_margin))
            : results_viewport;
    if (suggestions_->Root().MaxHeight() != bounded_results)
      suggestions_->Root().MaxHeight(bounded_results);
    if (command_popup_.HorizontalOffset() != bounds.x - client.x)
      command_popup_.HorizontalOffset(bounds.x - client.x);
    if (command_popup_.VerticalOffset() != bounds.y - client.y)
      command_popup_.VerticalOffset(bounds.y - client.y);
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Could not position the search box");
  }
}

void CommandBar::RenderCommandBar(const CommandBarVm& vm,
                                  Rect client,
                                  float scale,
                                  xaml::ElementTheme theme,
                                  const ImageStore* images) {
  rendering_ = true;
  struct RenderScope {
    bool& active;
    ~RenderScope() { active = false; }
  } scope{rendering_};
  if (!vm.open || client.empty()) {
    Close();
    return;
  }
  EnsureCommandBar();
  client_ = client;
  floating_ = vm.floating;
  menu_backdrop_ = vm.menu_backdrop;
  // Popups have a separate theme root from their owner island.
  surface_.RequestedTheme(theme);
  command_input_.RequestedTheme(theme);
  command_input_.Foreground(context_.brushes.search_input);
  command_input_.PlaceholderForeground(context_.brushes.search_description);
  const bool opening = !requested_open_;
  requested_open_ = true;
  if (!composing_ && command_input_.Text() != winrt::to_hstring(vm.text))
    command_input_.Text(winrt::to_hstring(vm.text));
  selection_pending_ = vm.selection;
  if (!composing_ &&
      (command_input_.SelectionStart() !=
           static_cast<std::int32_t>(vm.selection.start) ||
       command_input_.SelectionLength() !=
           static_cast<std::int32_t>(vm.selection.end - vm.selection.start))) {
    command_input_.Select(
        static_cast<std::int32_t>(vm.selection.start),
        static_cast<std::int32_t>(vm.selection.end - vm.selection.start));
  }
  suggestions_->Render(vm.suggestions, vm.selected, images);
  if (opening || command_serial_ != vm.open_serial) {
    command_serial_ = vm.open_serial;
    focus_pending_ = true;
  }
  island_.Show(client, scale, theme);
  OpenWhenLoaded();
}

}  // namespace kr::win
