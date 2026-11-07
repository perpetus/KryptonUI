#include "chrome_header.hpp"

#include <winrt/Microsoft.UI.Xaml.Input.h>

#include <cmath>
#include <limits>

#include "icon_view.hpp"
#include "krypton/ui/layout/horizontal_layout.hpp"
#include "krypton/ui/presentation/chrome_layout.hpp"

namespace kr::win {
using namespace controls;
using xaml::Thickness;
using xaml::Visibility;

bool ChromeHeader::FocusNative(Anchor target) {
  const auto focus = [](const controls::Control& control) {
    return control && control.IsEnabled() &&
           control.Visibility() == Visibility::Visible &&
           control.Focus(xaml::FocusState::Programmatic);
  };
  if (target.kind == AnchorKind::kAction) {
    for (auto* buttons : {&page_buttons_, &toolbar_buttons_}) {
      const auto found = buttons->find(ActionId{target.id});
      if (found != buttons->end())
        return focus(found->second->control);
    }
    return false;
  }
  if (target.kind != AnchorKind::kElement)
    return false;
  auto element = static_cast<Element>(target.id);
  if (element == Element::kAddress)
    return focus(address_);
  if (element == Element::kToolbar)
    return focus(back_->control) || focus(forward_->control) ||
           focus(reload_->control) || focus(address_);
  const NativeButton* button = nullptr;
  switch (element) {
    case Element::kBack:
      button = back_.get();
      break;
    case Element::kForward:
      button = forward_.get();
      break;
    case Element::kReload:
      button = reload_.get();
      break;
    case Element::kSecurityChip:
      button = security_button_.get();
      break;
    case Element::kAppMenu:
      button = app_menu_.get();
      break;
    case Element::kCompactMode:
      button = toggle_.get();
      break;
    default:
      break;
  }
  return button && focus(button->control);
}

ChromeHeader::ChromeHeader(ViewContext& context, bool& anchors_dirty)
    : context_(context),
      anchors_dirty_(anchors_dirty),
      progress_(progress_layer_) {
  for (const auto& panel : {leading_, navigation_, window_controls_, end_group_,
                            toolbar_, page_actions_}) {
    panel.Orientation(Orientation::Horizontal);
    panel.VerticalAlignment(xaml::VerticalAlignment::Center);
  }
  AddColumn(sidebar_top_, 0, xaml::GridUnitType::Auto);
  AddColumn(sidebar_top_, 1, xaml::GridUnitType::Star);
  AddColumn(sidebar_top_, 0, xaml::GridUnitType::Auto);
  AddColumn(toolbar_root_, 0, xaml::GridUnitType::Auto);
  AddColumn(toolbar_root_, 1, xaml::GridUnitType::Star);
  AddColumn(toolbar_root_, 0, xaml::GridUnitType::Auto);
  AddColumn(toolbar_root_, 0, xaml::GridUnitType::Auto);
  Grid::SetColumn(tab_slot_, 2);
  toolbar_root_.Children().Append(tab_slot_);
  address_slot_.Children().Append(address_host_);
  toolbar_sized_ = toolbar_root_.SizeChanged(
      winrt::auto_revoke, [this](auto&&, auto&&) { PlaceAddress(); });
  slot_sized_ = address_slot_.SizeChanged(
      winrt::auto_revoke, [this](auto&&, auto&&) { PlaceAddress(); });
  end_sized_ = end_group_.SizeChanged(
      winrt::auto_revoke, [this](auto&&, auto&&) { PlaceAddress(); });

  // Toolbar icons follow the chosen icon pack.
  const auto button = [&](IconRole role, std::wstring_view name, Task task) {
    return PackButton(context_.icons, context_.brushes, role, name,
                      std::move(task));
  };
  back_ = button(IconRole::kBack, NativeText(UiText::kBack),
                 [this] { context_.delegate.OnNavCommand(NavCommand::kBack); });
  forward_ = button(IconRole::kForward, NativeText(UiText::kForward), [this] {
    context_.delegate.OnNavCommand(NavCommand::kForward);
  });
  reload_ = button(IconRole::kReload, NativeText(UiText::kReload), [this] {
    context_.delegate.OnNavCommand(loading_ ? NavCommand::kStop
                                            : NavCommand::kReload);
  });
  ContextFor(*back_, MenuKind::kBackHistory);
  ContextFor(*forward_, MenuKind::kForwardHistory);
  ContextFor(*reload_, MenuKind::kReload);
  for (const auto* item : {back_.get(), forward_.get(), reload_.get()}) {
    item->control.Background(context_.brushes.transparent);
    for (const auto* key :
         {L"ButtonBackground", L"ButtonBackgroundPointerOver",
          L"ButtonBackgroundPressed", L"ButtonBackgroundDisabled",
          L"ButtonBorderBrush", L"ButtonBorderBrushPointerOver",
          L"ButtonBorderBrushPressed", L"ButtonBorderBrushDisabled"})
      item->control.Resources().Insert(winrt::box_value(winrt::hstring(key)),
                                       context_.brushes.transparent);
    item->control.Resources().Insert(
        winrt::box_value(L"ButtonForegroundDisabled"), context_.brushes.muted);
    navigation_.Children().Append(item->control);
  }

  const auto caption = [&](std::wstring_view glyph, std::wstring_view name,
                           WindowCommand command) {
    return CaptionButton(context_.brushes, glyph, name, [this, command] {
      context_.delegate.OnWindowCommand(command);
    });
  };
  minimize_ = caption(glyphs::kMinimize, NativeText(UiText::kMinimize),
                      WindowCommand::kMinimize);
  maximize_ = caption(glyphs::kMaximize, NativeText(UiText::kMaximize),
                      WindowCommand::kToggleMaximize);
  close_ = caption(glyphs::kClose, NativeText(UiText::kCloseWindow),
                   WindowCommand::kClose);
  const auto resources = close_->control.Resources();
  resources.Insert(winrt::box_value(L"ButtonBackgroundPointerOver"),
                   context_.brushes.close_hover);
  resources.Insert(winrt::box_value(L"ButtonBackgroundPressed"),
                   context_.brushes.close_pressed);
  resources.Insert(winrt::box_value(L"ButtonForegroundPointerOver"),
                   context_.brushes.close_glyph);
  resources.Insert(winrt::box_value(L"ButtonForegroundPressed"),
                   context_.brushes.close_glyph);
  for (const auto* item : {minimize_.get(), maximize_.get(), close_.get()})
    window_controls_.Children().Append(item->control);

  toggle_ = PackButton(
      context_.icons, context_.brushes, IconRole::kSidebar,
      NativeText(UiText::kCompactMode),
      [this] { context_.delegate.OnSidebarToggled(SidebarToggle::kCompact); });
  toggle_->control.HorizontalAlignment(xaml::HorizontalAlignment::Left);
  toggle_->control.Background(context_.brushes.transparent);
  leading_.Children().Append(toggle_->control);
  ContextFor(*toggle_, MenuKind::kSidebarLayout);
  sidebar_top_.Children().Append(leading_);

  app_menu_ = button(IconRole::kMore, NativeText(UiText::kAppMenu), [this] {
    context_.request_menu(MenuKind::kApp, 0,
                          context_.bounds(app_menu_->control));
  });
  app_menu_->icon->Root().RenderTransformOrigin({0.5f, 0.5f});
  app_menu_->icon->Root().RenderTransform(app_menu_rotation_);
  end_group_.Children().Append(toolbar_);
  end_group_.Children().Append(media_slot_);
  end_group_.Children().Append(library_slot_);
  end_group_.Children().Append(app_menu_->control);
  end_group_.Children().Append(window_controls_);

  AddColumn(address_content_, 0, xaml::GridUnitType::Auto);
  AddColumn(address_content_, 1, xaml::GridUnitType::Star);
  AddColumn(address_content_, 0, xaml::GridUnitType::Auto);
  address_host_.Children().Append(progress_layer_);
  address_host_.Children().Append(address_content_);
  security_button_ =
      button(IconRole::kSearch, NativeText(UiText::kSecurity), [this] {
        context_.request_menu(MenuKind::kSecurity, 0,
                              context_.bounds(security_button_->control));
      });
  address_content_.Children().Append(security_button_->control);
  address_text_ = Label(NativeText(UiText::kAddress), kTokens.type.size);
  address_text_.FontWeight(kMedium);
  address_text_.Foreground(context_.brushes.text);
  Grid::SetColumn(address_, 1);
  Grid::SetColumn(page_actions_, 2);
  address_content_.Children().Append(address_);
  address_content_.Children().Append(page_actions_);
  address_.Content(address_text_);
  address_.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  address_.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
  address_.Height(kTokens.spacing.pill_height);
  address_.MinWidth(0);
  address_.MinHeight(0);
  address_.Padding(UniformThickness(0));
  address_content_.Padding(Thickness{kTokens.spacing.field_padding, 0,
                                     kTokens.spacing.field_padding, 0});
  address_host_.Margin(Thickness{kTokens.spacing.urlbar_margin, 0,
                                 kTokens.spacing.urlbar_margin, 0});
  address_.BorderThickness(UniformThickness(0));
  address_.CornerRadius(UniformCorners(kTokens.radius.pill));
  address_.Background(context_.brushes.transparent);
  address_host_.Background(context_.brushes.pill);
  address_host_.CornerRadius(UniformCorners(kTokens.radius.pill));
  Accessible(address_, NativeText(UiText::kAddress));
  address_click_ = address_.Click(winrt::auto_revoke, [this](auto&&, auto&&) {
    if (address_.FocusState() == xaml::FocusState::Keyboard)
      context_.delegate.OnCommandBarRequested();
    else
      context_.delegate.OnAddressPointerRequested();
  });
  address_sized_ = address_host_.SizeChanged(
      winrt::auto_revoke, [this](auto&&, auto&&) { PlaceAddressActions(); });
  address_focused_ =
      address_host_.GotFocus(winrt::auto_revoke, [this](auto&&, auto&&) {
        context_.delegate.OnAddressFocusChanged(true);
      });
  address_blurred_ =
      address_host_.LostFocus(winrt::auto_revoke, [this](auto&&, auto&&) {
        const auto root = address_host_.XamlRoot();
        auto element = root ? xaml::Input::FocusManager::GetFocusedElement(root)
                                  .try_as<xaml::DependencyObject>()
                            : xaml::DependencyObject{nullptr};
        bool inside = false;
        while (element) {
          if (element == address_host_) {
            inside = true;
            break;
          }
          element = media::VisualTreeHelper::GetParent(element);
        }
        context_.delegate.OnAddressFocusChanged(inside);
      });
  SetSingleToolbar(false);
}

void ChromeHeader::ContextFor(NativeButton& button, MenuKind kind) {
  button.right_tapped = button.control.RightTapped(
      winrt::auto_revoke,
      [this, native = &button, kind](
          auto&&, const xaml::Input::RightTappedRoutedEventArgs& args) {
        context_.request_menu(kind, 0, context_.bounds(native->control));
        args.Handled(true);
      });
}

void ChromeHeader::SetSingleToolbar(bool single) {
  if (initialized_ && single_ == single)
    return;
  single_ = single;
  initialized_ = true;
  const auto attach = [&](const xaml::FrameworkElement& element,
                          const Grid& parent, int column) {
    for (const auto& old : {sidebar_top_, address_row_, actions_row_,
                            toolbar_root_, address_host_}) {
      std::uint32_t index;
      if (old.Children().IndexOf(element, index))
        old.Children().RemoveAt(index);
    }
    Grid::SetColumn(element, column);
    parent.Children().Append(element);
  };
  std::uint32_t menu_index;
  for (const auto& panel : {leading_, end_group_})
    if (panel.Children().IndexOf(app_menu_->control, menu_index))
      panel.Children().RemoveAt(menu_index);
  if (single)
    leading_.Children().InsertAt(0, app_menu_->control);
  else {
    (void)end_group_.Children().IndexOf(library_slot_, menu_index);
    end_group_.Children().InsertAt(menu_index + 1, app_menu_->control);
  }
  attach(navigation_, single ? sidebar_top_ : toolbar_root_, single ? 1 : 0);
  navigation_.HorizontalAlignment(single ? xaml::HorizontalAlignment::Right
                                         : xaml::HorizontalAlignment::Left);
  std::uint32_t caption_index;
  if (end_group_.Children().IndexOf(window_controls_, caption_index))
    end_group_.Children().RemoveAt(caption_index);
  attach(end_group_, single ? actions_row_ : toolbar_root_, single ? 0 : 3);
  if (single) {
    attach(window_controls_, sidebar_top_, 2);
  } else {
    if (sidebar_top_.Children().IndexOf(window_controls_, caption_index))
      sidebar_top_.Children().RemoveAt(caption_index);
    end_group_.Children().Append(window_controls_);
  }
  attach(address_slot_, single ? address_row_ : toolbar_root_, single ? 0 : 1);
  address_row_.Height(single ? kTokens.spacing.pill_height : 0);
  actions_row_.Height(single ? std::numeric_limits<double>::quiet_NaN() : 0);
  const auto height = single ? kTokens.spacing.single_header_height
                             : kTokens.spacing.header_height;
  sidebar_top_.Height(height);
  address_.Height(single ? kTokens.spacing.pill_height
                         : kTokens.spacing.field_height);
  address_host_.Height(address_.Height());
  for (const auto* button : {minimize_.get(), maximize_.get(), close_.get()})
    button->control.Height(height);
}

void ChromeHeader::SetLayout(const ShellLayout& layout) {
  SetSingleToolbar(layout.toolbar_layout == ToolbarLayout::kSingle);
  chrome_visible_ = single_ ? layout.sidebar_visible : layout.toolbar_visible;
  row_height_ = layout.chrome_row_height;
  if (sidebar_top_.Height() != row_height_)
    sidebar_top_.Height(row_height_);
  const double address_row_height = single_ ? row_height_ : 0;
  if (address_row_.Height() != address_row_height)
    address_row_.Height(address_row_height);
  const double field_height = std::min<double>(
      single_ ? kTokens.spacing.pill_height : kTokens.spacing.field_height,
      row_height_);
  if (address_.Height() != field_height)
    address_.Height(field_height);
  if (address_host_.Height() != field_height)
    address_host_.Height(field_height);
  const double menu_angle = layout.tabs == TabLayout::kVertical ? 0 : 90;
  if (app_menu_rotation_.Angle() != menu_angle)
    app_menu_rotation_.Angle(menu_angle);
  compact_tabs_ = layout.tabs == TabLayout::kCompact;
  centered_address_ = layout.centered_address && !single_ && !compact_tabs_;
  toolbar_root_.ColumnDefinitions().GetAt(2).Width(
      {compact_tabs_ ? 1.0 : 0.0,
       compact_tabs_ ? xaml::GridUnitType::Star : xaml::GridUnitType::Auto});
  PlaceAddress();
}

bool ChromeHeader::PlaceAddress() {
  if (placing_address_)
    return false;
  placing_address_ = true;
  struct Reset {
    bool& flag;
    ~Reset() { flag = false; }
  } reset{placing_address_};
  bool changed = false;
  const float width = static_cast<float>(toolbar_root_.ActualWidth());
  xaml::GridLength address_column{1, xaml::GridUnitType::Star};
  if (compact_tabs_) {
    const float available =
        std::max(0.0f, width - static_cast<float>(navigation_.ActualWidth() +
                                                  end_group_.ActualWidth()));
    const float minimum_address =
        2 * (kTokens.spacing.button_size + kTokens.spacing.field_padding);
    const float minimum_tabs =
        kTokens.spacing.tab_padding + kTokens.spacing.icon_size +
        kTokens.spacing.tab_icon_gap + kTokens.spacing.close_button_size +
        kTokens.spacing.button_size;
    address_column = {
        CompactAddressWidth(available, minimum_address, minimum_tabs),
        xaml::GridUnitType::Pixel};
  }
  const auto column = toolbar_root_.ColumnDefinitions().GetAt(1);
  if (column.Width() != address_column) {
    column.Width(address_column);
    changed = true;
  }
  const float inset = kTokens.spacing.urlbar_margin;
  double desired_width = std::numeric_limits<double>::quiet_NaN();
  auto alignment = xaml::HorizontalAlignment::Stretch;
  xaml::Thickness margin{inset, 0, inset, 0};
  if (centered_address_) {
    const float slot_width = static_cast<float>(address_slot_.ActualWidth());
    const float slot_x =
        address_slot_.TransformToVisual(toolbar_root_).TransformPoint({0, 0}).X;
    const Rect slot{slot_x + inset, 0, std::max(0.0f, slot_width - 2 * inset),
                    static_cast<float>(address_host_.ActualHeight())};
    const auto bounds = CenteredAddressBounds(
        slot, width,
        2 * (kTokens.spacing.button_size + kTokens.spacing.field_padding));
    desired_width = bounds.width;
    alignment = xaml::HorizontalAlignment::Left;
    margin = {bounds.x - slot_x, 0, 0, 0};
  }
  const auto current_width = address_host_.Width();
  if (std::isnan(desired_width) ? !std::isnan(current_width)
                                : current_width != desired_width) {
    address_host_.Width(desired_width);
    changed = true;
  }
  if (address_host_.HorizontalAlignment() != alignment) {
    address_host_.HorizontalAlignment(alignment);
    changed = true;
  }
  if (address_host_.Margin() != margin) {
    address_host_.Margin(margin);
    changed = true;
  }
  if (changed)
    anchors_dirty_ = true;
  return changed;
}

void ChromeHeader::Render(const ShellViewModel& vm) {
  window_active_ = vm.header.window_active;
  address_host_.Background(
      context_.glass.For(address_host_, GlassRole::kAddress,
                         context_.brushes.pill, kTokens.radius.pill));
  if (show_site_security_ != vm.header.show_site_security) {
    show_site_security_ = vm.header.show_site_security;
    security_button_->control.Visibility(
        show_site_security_ ? Visibility::Visible : Visibility::Collapsed);
    anchors_dirty_ = true;
  }
  progress_.Render(vm.loading_progress, vm.text.right_to_left,
                   context_.brushes.high_contrast(),
                   vm.changed.Has(Dirty::kTheme));
  toggle_->control.Visibility(vm.header.show_compact_toggle
                                  ? Visibility::Visible
                                  : Visibility::Collapsed);
  if (window_controls_visible_ != vm.header.show_window_controls) {
    window_controls_visible_ = vm.header.show_window_controls;
    window_controls_.Visibility(
        window_controls_visible_ ? Visibility::Visible : Visibility::Collapsed);
  }
  if (vm.changed.Has(Dirty::kNav) || vm.changed.Has(Dirty::kWindow) ||
      !rendered_) {
    back_->control.IsEnabled(vm.header.can_go_back);
    back_->icon->SetBrush(vm.header.can_go_back ? context_.brushes.icon
                                                : context_.brushes.muted);
    forward_->icon->SetBrush(vm.header.can_go_forward ? context_.brushes.icon
                                                      : context_.brushes.muted);
    forward_->control.IsEnabled(vm.header.can_go_forward);
    if (loading_ != vm.header.is_loading || !rendered_) {
      loading_ = vm.header.is_loading;
      SetButtonText(*reload_, loading_ ? stop_text_ : reload_text_);
    }
    if (maximized_ != vm.header.maximized || !rendered_) {
      maximized_ = vm.header.maximized;
      SetButtonText(*maximize_, maximized_ ? restore_text_ : maximize_text_);
    }
    reload_->icon->SetRole(loading_ ? IconRole::kStop : IconRole::kReload);
    maximize_->glyph.Text(winrt::hstring(
        vm.header.maximized ? glyphs::kRestore : glyphs::kMaximize));
    security_button_->icon->SetRole(
        vm.header.security == SecurityLevel::kNone     ? IconRole::kSearch
        : vm.header.security == SecurityLevel::kSecure ? IconRole::kLock
                                                       : IconRole::kInfo);
  }
  if (address_value_ != vm.header.address_text) {
    address_value_ = vm.header.address_text;
    address_text_.Text(address_value_.empty()
                           ? address_placeholder_
                           : winrt::to_hstring(address_value_));
  }
  if (vm.changed.Has(Dirty::kActions) || vm.changed.Has(Dirty::kImages) ||
      vm.changed.Has(Dirty::kLayout) || !rendered_) {
    images_ = vm.images;
    struct BorrowScope {
      const ImageStore*& images;
      ~BorrowScope() { images = nullptr; }
    } scope{images_};
    ReconcileActions(vm.toolbar_actions, toolbar_, toolbar_buttons_);
    ReconcileActions(vm.page_actions, page_actions_, page_buttons_);
    page_action_order_.clear();
    page_action_kinds_.clear();
    for (const auto& action : vm.page_actions) {
      page_action_order_.push_back(action.id);
      page_action_kinds_.push_back(action.kind);
    }
    page_action_visibility_.resize(page_action_order_.size());
    PlaceAddressActions();
  }
  const double actions_height =
      single_ && !vm.toolbar_actions.empty() ? row_height_ : 0;
  if (actions_row_.Height() != actions_height)
    actions_row_.Height(actions_height);
  RefreshToolsVisuals();
  rendered_ = true;
}

void ChromeHeader::SetText(UiTextView text) {
  const auto set = [&](NativeButton& button, UiText id) {
    SetButtonText(button, NativeText(id, text));
  };
  set(*back_, UiText::kBack);
  set(*forward_, UiText::kForward);
  set(*minimize_, UiText::kMinimize);
  set(*close_, UiText::kCloseWindow);
  set(*toggle_, UiText::kCompactMode);
  set(*security_button_, UiText::kSecurity);
  set(*app_menu_, UiText::kAppMenu);
  reload_text_ = NativeText(UiText::kReload, text);
  stop_text_ = NativeText(UiText::kStop, text);
  maximize_text_ = NativeText(UiText::kMaximize, text);
  restore_text_ = NativeText(UiText::kRestore, text);
  SetButtonText(*reload_, loading_ ? stop_text_ : reload_text_);
  SetButtonText(*maximize_, maximized_ ? restore_text_ : maximize_text_);
  address_placeholder_ = NativeText(UiText::kAddress, text);
  Accessible(address_, address_placeholder_);
  SetToolTip(address_, address_placeholder_);
  if (address_value_.empty())
    address_text_.Text(address_placeholder_);
}

void ChromeHeader::AppendAnchors(std::vector<Anchor>& anchors,
                                 bool visible,
                                 bool sidebar_visible) const {
  const auto add = [&](Element id, const xaml::FrameworkElement& native) {
    anchors.push_back({AnchorKind::kElement, static_cast<std::uint64_t>(id),
                       context_.bounds(native),
                       visible && native.Visibility() == Visibility::Visible});
  };
  add(Element::kAddress, address_host_);
  add(Element::kSecurityChip, security_button_->control);
  add(Element::kBack, back_->control);
  add(Element::kForward, forward_->control);
  add(Element::kReload, reload_->control);
  add(Element::kAppMenu, app_menu_->control);
  anchors.push_back({AnchorKind::kElement,
                     static_cast<std::uint64_t>(Element::kCompactMode),
                     context_.bounds(toggle_->control),
                     sidebar_visible &&
                         toggle_->control.Visibility() == Visibility::Visible});
  // The sidebar toggle remains in the sidebar when a top toolbar is present.
  for (const auto* buttons : {&page_buttons_, &toolbar_buttons_})
    for (const auto& [id, button] : *buttons)
      anchors.push_back(
          {AnchorKind::kAction, id.value, context_.bounds(button->control),
           visible && button->control.Visibility() == Visibility::Visible});
}

void ChromeHeader::AppendDragExclusions(std::vector<Rect>& controls,
                                        const ShellLayout& layout) const {
  const auto add = [&](const xaml::FrameworkElement& element) {
    if (element.Visibility() == Visibility::Visible)
      controls.push_back(context_.bounds(element));
  };
  if (single_ ? layout.sidebar_visible : layout.toolbar_visible) {
    add(navigation_);
    add(address_host_);
    if (layout.sidebar_visible)
      add(leading_);
    add(end_group_);
    add(window_controls_);
  }
  if (layout.sidebar_visible && !single_)
    add(leading_);
}

void ChromeHeader::PlaceAddressActions() {
  kr::PlaceAddressActions(
      page_action_kinds_,
      AddressActionCapacity(static_cast<float>(address_host_.ActualWidth())),
      page_action_visibility_);
  for (std::size_t i = 0; i < page_action_order_.size(); ++i) {
    const auto found = page_buttons_.find(page_action_order_[i]);
    if (found == page_buttons_.end())
      continue;
    const auto visibility = page_action_visibility_[i] ? Visibility::Visible
                                                       : Visibility::Collapsed;
    if (found->second->control.Visibility() != visibility) {
      found->second->control.Visibility(visibility);
      anchors_dirty_ = true;
    }
  }
}

void ChromeHeader::ReconcileActions(
    std::span<const ActionVm> actions,
    StackPanel panel,
    std::unordered_map<ActionId, std::unique_ptr<NativeButton>>& buttons) {
  const auto seen = ++button_generation_;
  std::uint32_t index = 0;
  for (const auto& action : actions) {
    auto& button = buttons[action.id];
    if (!button) {
      button =
          IconButton(context_.brushes, L"\uE713",
                     winrt::to_hstring(action.label), [this, id = action.id] {
                       const auto find = [id](auto& map) -> NativeButton* {
                         auto it = map.find(id);
                         return it == map.end() ? nullptr : it->second.get();
                       };
                       auto* native = find(page_buttons_);
                       if (!native)
                         native = find(toolbar_buttons_);
                       if (native)
                         context_.delegate.OnActionInvoked(
                             id, context_.bounds(native->control));
                     });
      button->right_tapped = button->control.RightTapped(
          winrt::auto_revoke,
          [this, id = action.id, native = button.get()](
              auto&&, const xaml::Input::RightTappedRoutedEventArgs& args) {
            context_.request_menu(MenuKind::kAction, id.value,
                                  context_.bounds(native->control));
            args.Handled(true);
          });
      panel.Children().InsertAt(index, button->control);
    }
    button->seen = seen;
    std::uint32_t position;
    if (panel.Children().IndexOf(button->control, position) &&
        position != index) {
      panel.Children().RemoveAt(position);
      panel.Children().InsertAt(index, button->control);
    }
    const auto glyph = action.glyph.empty() ? winrt::hstring(L"\uE713")
                                            : winrt::to_hstring(action.glyph);
    if (button->glyph.Text() != glyph)
      button->glyph.Text(glyph);
    const auto current = ActionIcon(*button, action.id);
    xaml::UIElement icon = button->glyph;
    if (action.kind == ActionKind::kSiteTools) {
      auto [entry, created] = tools_visuals_.try_emplace(action.id);
      auto& visual = entry->second;
      if (created) {
        visual.root = Grid();
        visual.root.Width(kTokens.spacing.action_icon_size);
        visual.root.Height(kTokens.spacing.action_icon_size);
        visual.root.IsHitTestVisible(false);
        visual.normal = SourceIcon(context_.brushes, design::icons::kSiteTools);
        visual.boosted =
            SourceIcon(context_.brushes, design::icons::kSiteToolsActive);
        visual.root.Children().Append(visual.normal);
        visual.root.Children().Append(visual.boosted);
        visual.loaded = visual.root.Loaded(
            winrt::auto_revoke,
            [this](auto&&, auto&&) { RefreshToolsVisuals(); });
        visual.unloaded = visual.root.Unloaded(
            winrt::auto_revoke, [this, id = action.id](auto&&, auto&&) {
              const auto found = tools_visuals_.find(id);
              if (found != tools_visuals_.end() && found->second.indicator)
                (void)found->second.indicator->Loop(false, 0);
            });
      }
      if (created || visual.active != action.active) {
        visual.active = action.active;
        visual.normal.Visibility(action.active ? Visibility::Collapsed
                                               : Visibility::Visible);
        visual.boosted.Visibility(action.active ? Visibility::Visible
                                                : Visibility::Collapsed);
      }
      icon = visual.root;
    } else if (auto bitmap = Bitmap(action.icon)) {
      auto image = current.try_as<controls::Image>();
      if (!image) {
        // Extension icons arrive as Chromium's toolbar image: the 16 DIP
        // icon centred in padding. Show the whole image unscaled so the icon
        // matches the glyphs beside it.
        image = controls::Image();
        image.Width(kTokens.spacing.action_icon_size);
        image.Height(kTokens.spacing.action_icon_size);
      }
      if (image.Source() != bitmap)
        image.Source(bitmap);
      icon = image;
    }
    ShowActionIcon(*button, action, icon);
    button->control.IsEnabled(action.enabled);
    button->control.Background(action.active &&
                                       action.kind != ActionKind::kSiteTools
                                   ? context_.brushes.active
                                   : context_.brushes.transparent);
    if (button->tooltip_text != action.tooltip) {
      button->tooltip_text = action.tooltip;
      ToolTipService::SetToolTip(
          button->control, winrt::box_value(winrt::to_hstring(action.tooltip)));
    }
    ++index;
  }
  std::erase_if(buttons, [&](auto& entry) {
    if (entry.second->seen == seen)
      return false;
    std::uint32_t position;
    if (panel.Children().IndexOf(entry.second->control, position))
      panel.Children().RemoveAt(position);
    action_badges_.erase(entry.first);
    tools_visuals_.erase(entry.first);
    return true;
  });
}

void ChromeHeader::RefreshToolsVisuals() {
  const auto& tokens = kTokens.boost_tools;
  const bool animate =
      window_active_ && chrome_visible_ &&
      context_.motion.Enabled(context_.brushes.high_contrast());
  for (auto& [id, visual] : tools_visuals_) {
    const bool running = visual.active && animate && visual.root.IsLoaded();
    if (running && !visual.indicator) {
      visual.indicator = std::make_unique<SpriteIcon>(Sprite::kBoostIndicator,
                                                      tokens.indicator_size);
      visual.root.Children().Append(visual.indicator->Root());
    }
    if (!visual.indicator)
      continue;
    const auto native_color = context_.brushes.text.Color();
    const auto color = Color::FromArgb(native_color.A, native_color.R,
                                       native_color.G, native_color.B);
    const auto root = visual.root.XamlRoot();
    visual.indicator->SetColors(
        color, color, root ? static_cast<float>(root.RasterizationScale()) : 1);
    visual.indicator->Root().Visibility(running ? Visibility::Visible
                                                : Visibility::Collapsed);
    (void)visual.indicator->Loop(running, tokens.animation_ms);
  }
}
xaml::UIElement ChromeHeader::ActionIcon(const NativeButton& button,
                                         ActionId id) const {
  const auto content = button.control.Content().try_as<xaml::UIElement>();
  const auto found = action_badges_.find(id);
  if (found == action_badges_.end() || content != found->second.root)
    return content;
  return found->second.root.Children().GetAt(0);
}

void ChromeHeader::ShowActionIcon(NativeButton& button,
                                  const ActionVm& action,
                                  const xaml::UIElement& icon) {
  auto found = action_badges_.find(action.id);
  if (found == action_badges_.end()) {
    if (action.badge.empty()) {
      if (button.control.Content() != icon)
        button.control.Content(icon);
      return;
    }
    const auto& t = kTokens.action_badge;
    ActionBadge created;
    created.text = Label({}, t.font_size);
    created.text.FontWeight(kBold);
    created.text.LineHeight(t.font_size);
    created.text.LineStackingStrategy(
        xaml::LineStackingStrategy::BlockLineHeight);
    created.text.TextAlignment(xaml::TextAlignment::Center);
    created.text.TextTrimming(xaml::TextTrimming::None);
    created.badge = Border();
    created.badge.MinWidth(t.min_width);
    created.badge.MaxWidth(t.max_width);
    created.badge.Padding(
        {t.padding_inline, 0, t.padding_inline, t.padding_bottom});
    created.badge.CornerRadius(UniformCorners(t.radius));
    created.badge.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    created.badge.VerticalAlignment(xaml::VerticalAlignment::Top);
    // The offsets are from the 16 DIP icon, centred in the icon box.
    const float inset =
        (kTokens.spacing.action_icon_size - kTokens.spacing.icon_size) / 2;
    created.badge.Margin({0, t.offset_top + inset, t.offset_end + inset, 0});
    created.badge.Child(created.text);
    created.root = Grid();
    created.root.Width(kTokens.spacing.action_icon_size);
    created.root.Height(kTokens.spacing.action_icon_size);
    // Detach the icon from the button before the grid adopts it.
    button.control.Content(nullptr);
    created.root.Children().Append(icon);
    created.root.Children().Append(created.badge);
    button.control.Content(created.root);
    found = action_badges_.emplace(action.id, std::move(created)).first;
  }
  auto& badge = found->second;
  if (badge.root.Children().GetAt(0) != icon)
    badge.root.Children().SetAt(0, icon);
  badge.badge.Visibility(action.badge.empty() ? Visibility::Collapsed
                                              : Visibility::Visible);
  if (action.badge.empty())
    return;
  const auto text = winrt::to_hstring(action.badge);
  if (badge.text.Text() != text)
    badge.text.Text(text);
  const auto& t = kTokens.action_badge;
  auto& brushes = context_.brushes;
  const auto background =
      brushes.high_contrast()
          ? brushes.text
          : brushes.For(action.badge_color.a() ? action.badge_color
                                               : Color{t.background});
  const auto foreground =
      brushes.high_contrast()
          ? brushes.menu_background
          : brushes.For(action.badge_text_color.a() ? action.badge_text_color
                                                    : Color{t.foreground});
  if (badge.badge.Background() != background)
    badge.badge.Background(background);
  if (badge.text.Foreground() != foreground)
    badge.text.Foreground(foreground);
}

}  // namespace kr::win
