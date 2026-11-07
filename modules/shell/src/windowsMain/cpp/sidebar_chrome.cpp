#include "sidebar_chrome.hpp"

#include <windows.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

#include "krypton/ui/design/sprites.generated.hpp"
#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
using namespace controls;
using xaml::Thickness;
using xaml::Visibility;

bool SidebarChrome::FocusNative(Element target) {
  const NativeButton* button = nullptr;
  switch (target) {
    case Element::kNewTab:
      button = new_tab_.get();
      break;
    case Element::kWorkspace:
      button = workspace_.get();
      break;
    case Element::kLibrary:
      button = library_.get();
      break;
    case Element::kCreate:
      button = create_.get();
      break;
    default:
      break;
  }
  return button && button->control.IsEnabled() &&
         button->control.Visibility() == Visibility::Visible &&
         button->control.Focus(xaml::FocusState::Programmatic);
}

SidebarChrome::SidebarChrome(ViewContext& context,
                             ChromeHeader& header,
                             const xaml::UIElement& tabs)
    : context_(context), gesture_(context, root_) {
  root_.Background(context_.brushes.transparent);
  root_.Padding(Thickness{kTokens.spacing.sidebar_padding, 0,
                          kTokens.spacing.sidebar_padding, 0});
  for (int index = 0; index < 5; ++index)
    AddRow(root_, 0, xaml::GridUnitType::Auto);
  AddRow(root_, 1, xaml::GridUnitType::Star);
  AddRow(root_, 0, xaml::GridUnitType::Auto);  // collapsed media cards
  AddRow(root_, kTokens.spacing.toolbar_height, xaml::GridUnitType::Pixel);
  const auto append = [&](const xaml::UIElement& element, int row) {
    Grid::SetRow(element.as<xaml::FrameworkElement>(), row);
    root_.Children().Append(element);
  };
  append(header.SidebarTop(), 0);
  append(header.AddressRow(), 1);
  append(header.ActionsRow(), 2);
  workspace_glyph_ = Glyph(L"");
  workspace_glyph_.IsColorFontEnabled(false);
  workspace_glyph_.Visibility(Visibility::Collapsed);
  // The default workspace mark follows the icon pack; incognito shows an eye.
  workspace_icon_ = std::make_unique<IconView>(context_.icons, IconRole::kHome,
                                               kTokens.spacing.icon_size,
                                               context_.brushes.icon);
  Grid workspace_lead;
  workspace_lead.Children().Append(workspace_glyph_);
  workspace_lead.Children().Append(workspace_icon_->Root());
  workspace_ = RowButton(
      context_.brushes, workspace_lead, NativeText(UiText::kWorkspace),
      NativeText(UiText::kSwitchWorkspace), [this] {
        context_.request_menu(MenuKind::kWorkspaces, active_space_.value,
                              context_.bounds(workspace_->control));
      });
  const auto workspace_content = workspace_->control.Content().as<Grid>();
  AddColumn(workspace_content, 0, xaml::GridUnitType::Auto);
  dropdown_icon_ = std::make_unique<IconView>(
      context_.icons, IconRole::kChevronDown, kTokens.spacing.icon_size,
      context_.brushes.muted);
  Grid::SetColumn(dropdown_icon_->Root(), 2);
  workspace_content.Children().Append(dropdown_icon_->Root());
  workspace_->label.FontWeight(kMedium);
  workspace_->control.Content().as<Grid>().ColumnSpacing(
      kTokens.spacing.workspace_gap);
  workspace_->label.FontSize(kTokens.type.size_workspace);
  workspace_->control.Height(kTokens.spacing.workspace_height);
  workspace_->control.Padding(
      Thickness{kTokens.spacing.tab_padding + kTokens.spacing.workspace_inset,
                0, kTokens.spacing.tab_padding, 0});
  workspace_->right_tapped = workspace_->control.RightTapped(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::RightTappedRoutedEventArgs& args) {
        context_.request_menu(MenuKind::kSpace, active_space_.value,
                              context_.bounds(workspace_->control));
        args.Handled(true);
      });
  append(workspace_->control, 3);
  auto new_tab_icon = std::make_unique<IconView>(context_.icons, IconRole::kAdd,
                                                 kTokens.spacing.icon_size,
                                                 context_.brushes.icon);
  new_tab_ = RowButton(context_.brushes, new_tab_icon->Root(),
                       NativeText(UiText::kNewTab), NativeText(UiText::kNewTab),
                       [this] { context_.delegate.OnNewTab(); });
  new_tab_->icon.reset(new_tab_icon.release());
  // The tab list places this stable control immediately below its separator.
  append(tabs, 5);
  // Media cards and the downloads stack lie over the tab list's foot; the
  // spacer row below it keeps the collapsed cards clear of the last tab.
  media_ = std::make_unique<MediaStack>(context_);
  header.MediaSlot().Children().Append(media_->ToolbarButton());
  Grid::SetRowSpan(media_->Root(), 2);
  Canvas::SetZIndex(media_->Root(), 1);
  append(media_->Root(), 5);
  append(media_->Spacer(), 6);
  AddColumn(footer_, 0, xaml::GridUnitType::Auto);
  AddColumn(footer_, 1, xaml::GridUnitType::Star);
  AddColumn(footer_, 0, xaml::GridUnitType::Auto);
  // Zen's library button: an animated sprite that opens the library; the
  // right-click menu keeps the host's library commands.
  library_ =
      IconButton(context_.brushes, L"", NativeText(UiText::kLibrary), [this] {
        DismissHoverPopups();
        context_.delegate.OnLibraryToggled();
      });
  library_sprite_ = std::make_unique<SpriteIcon>(
      Sprite::kLibrary,
      kTokens.library.button_sprite_size * kTokens.library.button_sprite_scale);
  library_->control.Content(library_sprite_->Root());
  library_->right_tapped = library_->control.RightTapped(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::RightTappedRoutedEventArgs& args) {
        DismissHoverPopups();
        context_.request_menu(MenuKind::kLibrary, 0,
                              context_.bounds(library_->control));
        args.Handled(true);
      });
  library_entered_ = library_->control.PointerEntered(
      winrt::auto_revoke, [this](auto&&, auto&&) { context_.hover_changed(); });
  library_exited_ = library_->control.PointerExited(
      winrt::auto_revoke, [this](auto&&, auto&&) { context_.hover_changed(); });
  workspace_icons_.Orientation(Orientation::Horizontal);
  workspace_icons_.Spacing(kTokens.workspace_footer.gap);
  workspace_icons_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  workspace_icons_.VerticalAlignment(xaml::VerticalAlignment::Center);
  workspace_scroll_.HorizontalScrollBarVisibility(ScrollBarVisibility::Hidden);
  workspace_scroll_.VerticalScrollBarVisibility(ScrollBarVisibility::Disabled);
  workspace_scroll_.HorizontalScrollMode(ScrollMode::Enabled);
  workspace_scroll_.Content(workspace_icons_);
  Grid::SetColumn(workspace_scroll_, 1);
  footer_.Children().Append(workspace_scroll_);
  create_ =
      PackButton(context_.icons, context_.brushes, IconRole::kAdd,
                 NativeText(UiText::kCreate), [this] {
                   context_.request_menu(MenuKind::kCreate, 0,
                                         context_.bounds(create_->control));
                 });
  Grid::SetColumn(create_->control, 2);
  library_cell_.Children().Append(library_->control);
  footer_.Children().Append(library_cell_);
  arrival_host_.Width(0);
  arrival_host_.Height(0);
  arrival_host_.HorizontalAlignment(xaml::HorizontalAlignment::Left);
  arrival_host_.VerticalAlignment(xaml::VerticalAlignment::Center);
  arrival_host_.IsHitTestVisible(false);
  arrival_.Width(kTokens.download_arrival.box_size);
  arrival_.Height(kTokens.download_arrival.box_size);
  arrival_.CornerRadius(UniformCorners(kTokens.download_arrival.radius));
  arrival_.Child(Glyph(L"\uE896", kTokens.download_arrival.icon_size));
  arrival_.Opacity(0);
  arrival_host_.Children().Append(arrival_);
  Canvas::SetZIndex(arrival_host_, 20);
  library_cell_.Children().Append(arrival_host_);
  footer_.Children().Append(create_->control);
  downloads_ = std::make_unique<DownloadStack>(context_, library_->control,
                                               tabs, *media_, media_->Spacer());
  library_cell_.Children().Append(downloads_->Badge());
  Grid::SetRowSpan(downloads_->List(), 2);
  Canvas::SetZIndex(downloads_->List(), 2);
  append(downloads_->List(), 5);
  append(footer_, 7);
}

void SidebarChrome::RenderLibrary(const ShellViewModel& vm) {
  media_->Render(vm);
  if (vm.changed.HasAny(Dirty::kLibrary | Dirty::kImages | Dirty::kTheme |
                        Dirty::kText | Dirty::kLayout | Dirty::kWindow))
    downloads_->Render(vm);
  if (download_start_serial_ != vm.library.download_start_serial) {
    download_start_serial_ = vm.library.download_start_serial;
    if (download_start_serial_)
      DownloadStarted(vm.library);
  }
}

void SidebarChrome::RefreshHoverPopups() {
  const bool hovered = context_.pointer_over(library_->control);
  if (library_hovered_ != hovered) {
    library_hovered_ = hovered;
    if (context_.motion.Enabled(context_.brushes.high_contrast()))
      library_sprite_->PlayTo(hovered ? design::sprites::kFrames - 1 : 0,
                              hovered ? kTokens.library.button_hover_ms
                                      : kTokens.library.button_unhover_ms);
    else
      library_sprite_->ShowFrame(0);
  }
  downloads_->RefreshPointerHover();
  media_->RefreshPointerHover();
}

void SidebarChrome::DismissHoverPopups(const xaml::FrameworkElement& keep) {
  if (keep != downloads_->List())
    downloads_->DismissHover();
  if (keep != media_->Root())
    media_->DismissHover();
}

bool SidebarChrome::HasActiveHoverPopup() const noexcept {
  return downloads_->HasActivePopup() || media_->HasActivePopup();
}

void SidebarChrome::SetLibraryPlacement(bool horizontal,
                                        const controls::Grid& toolbar_slot) {
  if (horizontal_library_ == horizontal)
    return;
  downloads_->SetHorizontal(horizontal, root_);
  media_->SetHorizontal(horizontal, root_);
  const auto& old = horizontal_library_ ? toolbar_slot : footer_;
  std::uint32_t index;
  if (old.Children().IndexOf(library_cell_, index))
    old.Children().RemoveAt(index);
  (horizontal ? toolbar_slot : footer_).Children().Append(library_cell_);
  horizontal_library_ = horizontal;
}

void SidebarChrome::DownloadStarted(const LibraryVm& library) {
  if (!context_.motion.Enabled(context_.brushes.high_contrast()) ||
      !library_->control.IsLoaded())
    return;
  const auto& t = kTokens.download_arrival;
  arrival_.Background(context_.brushes.For(library.palette.accent));
  arrival_.Child().as<TextBlock>().Foreground(
      context_.brushes.For(library.palette.accent_text));
  Canvas::SetLeft(arrival_, (library_->control.ActualWidth() - t.box_size) / 2);
  Canvas::SetTop(arrival_, -t.box_size / 2);
  xaml::Hosting::ElementCompositionPreview::SetIsTranslationEnabled(arrival_,
                                                                    true);
  const auto visual =
      xaml::Hosting::ElementCompositionPreview::GetElementVisual(arrival_);
  const auto compositor = visual.Compositor();
  visual.CenterPoint({t.box_size / 2, t.box_size / 2, 0});
  auto motion = compositor.CreateVector3KeyFrameAnimation();
  motion.InsertKeyFrame(0, {0, -t.offset, 0});
  motion.InsertKeyFrame(1, {0, 0, 0});
  motion.Duration(Ms(t.duration_ms));
  visual.StartAnimation(L"Translation", motion);
  auto scale = compositor.CreateVector3KeyFrameAnimation();
  scale.InsertKeyFrame(0, {1, 1, 1});
  scale.InsertKeyFrame(1, {t.landing_scale, t.landing_scale, 1});
  scale.Duration(Ms(t.duration_ms));
  visual.StartAnimation(L"Scale", scale);
  auto opacity = compositor.CreateScalarKeyFrameAnimation();
  opacity.InsertKeyFrame(0, 0);
  opacity.InsertKeyFrame(.1f, 1);
  opacity.InsertKeyFrame(.8f, 1);
  opacity.InsertKeyFrame(1, 0);
  opacity.Duration(Ms(t.duration_ms));
  visual.StartAnimation(L"Opacity", opacity);
  const auto target =
      xaml::Hosting::ElementCompositionPreview::GetElementVisual(
          library_->control);
  target.CenterPoint({static_cast<float>(library_->control.ActualWidth()) / 2,
                      static_cast<float>(library_->control.ActualHeight()) / 2,
                      0});
  auto bounce = compositor.CreateVector3KeyFrameAnimation();
  bounce.InsertKeyFrame(0, {1, 1, 1});
  bounce.InsertKeyFrame(.35f, {t.bounce_scale, t.bounce_scale, 1});
  bounce.InsertKeyFrame(1, {1, 1, 1});
  bounce.DelayTime(Ms(t.duration_ms * .65f));
  bounce.Duration(Ms(t.bounce_ms));
  target.StartAnimation(L"Scale", bounce);
  library_sprite_->Play(0, design::sprites::kFrames - 1, t.duration_ms,
                        library_hovered_);
}

void SidebarChrome::SetLibraryIcon(const ThemeVm& theme, float scale) {
  // Zen fills the button's sprite with the toolbar icon colour.
  const auto native = context_.brushes.icon.Color();
  const auto icon = Color::FromArgb(native.A, native.R, native.G, native.B);
  library_sprite_->SetColors(icon, icon, scale,
                             theme.dark && !context_.brushes.high_contrast());
  if (!context_.motion.Enabled(context_.brushes.high_contrast()))
    library_sprite_->ShowFrame(0);
}

SidebarChrome::~SidebarChrome() = default;

void SidebarChrome::SetWorkspaceSwipeEnabled(bool enabled) {
  if (workspace_swipe_enabled_ == enabled)
    return;
  workspace_swipe_enabled_ = enabled;
  gesture_.Configure(enabled);
  workspace_scroll_.HorizontalScrollMode(enabled ? ScrollMode::Disabled
                                                 : ScrollMode::Enabled);
}

void SidebarChrome::SetIncognito(bool incognito) {
  if (incognito_ == incognito)
    return;
  incognito_ = incognito;
  workspace_icon_->SetRole(incognito ? IconRole::kIncognito : IconRole::kHome);
  workspace_glyph_.Visibility(!incognito && workspace_symbol_
                                  ? Visibility::Visible
                                  : Visibility::Collapsed);
  workspace_icon_->Root().Visibility(incognito || !workspace_symbol_
                                         ? Visibility::Visible
                                         : Visibility::Collapsed);
}

void SidebarChrome::SetIconOnly(bool icons) {
  if (icons_ == icons)
    return;
  icons_ = icons;
  dropdown_icon_->Root().Visibility(icons ? Visibility::Collapsed
                                          : Visibility::Visible);
  media_->SetCompact(icons);
  downloads_->SetCompact(icons);
  workspace_scroll_.Visibility(icons ? Visibility::Collapsed
                                     : Visibility::Visible);
  const auto padding = icons ? kTokens.sidebar.collapsed_padding
                             : kTokens.spacing.sidebar_padding;
  root_.Padding(Thickness{padding, 0, padding, 0});
  for (const auto* row : {workspace_.get(), new_tab_.get()}) {
    row->label.Visibility(icons ? Visibility::Collapsed : Visibility::Visible);
    row->control.HorizontalContentAlignment(
        icons ? xaml::HorizontalAlignment::Center
              : xaml::HorizontalAlignment::Stretch);
    const auto content = row->control.Content().as<Grid>();
    content.ColumnSpacing(icons ? 0
                          : row == workspace_.get()
                              ? kTokens.spacing.workspace_gap
                              : kTokens.spacing.tab_icon_gap);
    content.ColumnDefinitions().GetAt(1).Width(
        {icons ? 0.0 : 1.0,
         icons ? xaml::GridUnitType::Pixel : xaml::GridUnitType::Star});
    const auto leading =
        kTokens.spacing.tab_padding +
        (row == workspace_.get() ? kTokens.spacing.workspace_inset : 0);
    row->control.Padding(
        icons ? UniformThickness(0)
              : Thickness{leading, 0, kTokens.spacing.tab_padding, 0});
  }
  workspace_->control.Height(icons ? kTokens.spacing.workspace_collapsed_height
                                   : kTokens.spacing.workspace_height);
  footer_.ColumnDefinitions().Clear();
  if (icons) {
    AddColumn(footer_, 1, xaml::GridUnitType::Star);
  } else {
    AddColumn(footer_, 0, xaml::GridUnitType::Auto);
    AddColumn(footer_, 1, xaml::GridUnitType::Star);
    AddColumn(footer_, 0, xaml::GridUnitType::Auto);
  }
  for (const auto* button : {library_.get(), create_.get()})
    button->control.HorizontalAlignment(icons
                                            ? xaml::HorizontalAlignment::Center
                                            : xaml::HorizontalAlignment::Left);
  Grid::SetColumn(create_->control, icons ? 0 : 2);
  Grid::SetRow(create_->control, icons ? 1 : 0);
  if (icons) {
    AddRow(footer_, 0, xaml::GridUnitType::Auto);
    AddRow(footer_, 0, xaml::GridUnitType::Auto);
  } else {
    footer_.RowDefinitions().Clear();
  }
  root_.RowDefinitions().GetAt(7).Height(
      {icons ? 0 : kTokens.spacing.toolbar_height,
       icons ? xaml::GridUnitType::Auto : xaml::GridUnitType::Pixel});
}

void SidebarChrome::SetText(UiTextView text) {
  switch_text_ = NativeText(UiText::kSwitchWorkspace, text);
  SetButtonText(*new_tab_, NativeText(UiText::kNewTab, text));
  SetButtonText(*library_, NativeText(UiText::kLibrary, text));
  SetButtonText(*create_, NativeText(UiText::kCreate, text));
  media_->SetText(text);
  downloads_->SetText(text);
  if (!active_space_)
    SetButtonText(*workspace_, NativeText(UiText::kWorkspace, text));
  SetToolTip(workspace_->control,
             workspace_->label.Text() + L"\n" + switch_text_);
}

void SidebarChrome::Render(std::span<const SpaceVm> spaces) {
  const auto generation = ++space_generation_;
  std::uint32_t index = 0;
  for (const auto& space : spaces) {
    auto& button = space_buttons_[space.id];
    const bool inserted = !button;
    const auto indicator = space.glyph.empty() ? winrt::hstring(L"\uE915")
                                               : winrt::to_hstring(space.glyph);
    if (!button) {
      button = IconButton(
          context_.brushes, indicator, winrt::to_hstring(space.name),
          [this, id = space.id] { context_.delegate.OnSpaceSelected(id); });
      button->control.Width(kTokens.workspace_footer.button_size);
      button->control.Height(kTokens.workspace_footer.button_size);
      button->control.Margin(UniformThickness(0));
      button->glyph.FontSize(kTokens.workspace_footer.icon_size);
      button->glyph.IsColorFontEnabled(false);
      button->right_tapped = button->control.RightTapped(
          winrt::auto_revoke,
          [this, id = space.id, native = button.get()](
              auto&&, const xaml::Input::RightTappedRoutedEventArgs& args) {
            context_.request_menu(MenuKind::kSpace, id.value,
                                  context_.bounds(native->control));
            args.Handled(true);
          });
    }
    std::uint32_t previous;
    if (workspace_icons_.Children().IndexOf(button->control, previous) &&
        previous != index) {
      workspace_icons_.Children().RemoveAt(previous);
      workspace_icons_.Children().InsertAt(index, button->control);
    }
    button->seen = generation;
    const auto name = winrt::to_hstring(space.name);
    SetButtonText(*button, name);
    if (inserted || button->glyph.Text() != indicator) {
      button->glyph.Text(indicator);
      const bool symbol = indicator.size() == 1 && indicator[0] >= 0xE000 &&
                          indicator[0] <= 0xF8FF;
      if (!inserted || !symbol)
        button->glyph.FontFamily(media::FontFamily(winrt::to_hstring(
            symbol ? kTokens.type.icon_family : kTokens.type.family)));
    }
    button->glyph.Foreground(!context_.brushes.high_contrast() &&
                                     space.icon_color.a()
                                 ? context_.brushes.For(space.icon_color)
                                 : context_.brushes.workspace_icon);
    button->control.Background(space.active ? context_.brushes.active
                                            : context_.brushes.transparent);
    if (inserted)
      workspace_icons_.Children().InsertAt(index, button->control);
    ++index;
  }
  std::erase_if(space_buttons_, [&](auto& entry) {
    if (entry.second->seen == generation)
      return false;
    std::uint32_t position;
    if (workspace_icons_.Children().IndexOf(entry.second->control, position))
      workspace_icons_.Children().RemoveAt(position);
    return true;
  });
  for (const auto& space : spaces) {
    if (!space.active)
      continue;
    active_space_ = space.id;
    const auto name = winrt::to_hstring(space.name);
    if (workspace_->label.Text() != name) {
      workspace_->label.Text(name);
      Accessible(workspace_->control, name);
      SetToolTip(workspace_->control, name + L"\n" + switch_text_);
    }
    // A host glyph (symbol or emoji) wins; otherwise the pack's home icon.
    const auto glyph = winrt::to_hstring(space.glyph);
    workspace_symbol_ = !glyph.empty();
    if (workspace_glyph_.Text() != glyph) {
      workspace_glyph_.Text(glyph);
      const bool symbol =
          glyph.size() == 1 && glyph[0] >= 0xE000 && glyph[0] <= 0xF8FF;
      workspace_glyph_.FontFamily(media::FontFamily(winrt::to_hstring(
          symbol ? kTokens.type.icon_family : kTokens.type.family)));
    }
    workspace_glyph_.Visibility(!incognito_ && workspace_symbol_
                                    ? Visibility::Visible
                                    : Visibility::Collapsed);
    workspace_icon_->Root().Visibility(incognito_ || !workspace_symbol_
                                           ? Visibility::Visible
                                           : Visibility::Collapsed);
    const auto tint = !context_.brushes.high_contrast() && space.icon_color.a()
                          ? context_.brushes.For(space.icon_color)
                          : context_.brushes.workspace_icon;
    workspace_glyph_.Foreground(tint);
    workspace_icon_->SetBrush(tint);
    return;
  }
}

void SidebarChrome::AppendLibraryAnchor(std::vector<Anchor>& anchors,
                                        bool visible) const {
  anchors.push_back({AnchorKind::kElement,
                     static_cast<std::uint64_t>(Element::kLibrary),
                     context_.bounds(library_->control), visible});
}

void SidebarChrome::AppendAnchors(std::vector<Anchor>& anchors,
                                  bool visible) const {
  anchors.push_back({AnchorKind::kElement,
                     static_cast<std::uint64_t>(Element::kNewTab),
                     context_.bounds(new_tab_->control), visible});
  AppendLibraryAnchor(anchors, visible);
  anchors.push_back({AnchorKind::kElement,
                     static_cast<std::uint64_t>(Element::kCreate),
                     context_.bounds(create_->control), visible});
  anchors.push_back({AnchorKind::kElement,
                     static_cast<std::uint64_t>(Element::kWorkspace),
                     context_.bounds(workspace_->control), visible});
}

}  // namespace kr::win
