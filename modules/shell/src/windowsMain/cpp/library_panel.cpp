#include "library_panel.hpp"

#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.System.h>

#include <algorithm>
#include <cmath>

#include "icon_view.hpp"
#include "krypton/ui/design/sprites.generated.hpp"
#include "krypton/ui/platform/platform.hpp"
#include "native_theme.hpp"

namespace kr::win {
namespace {
namespace composition = winrt::Microsoft::UI::Composition;
constexpr auto& kLibrary = design::kTokens.library;

void Show(const xaml::UIElement& element, bool visible) {
  const auto value =
      visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
  if (element.Visibility() != value)
    element.Visibility(value);
}

Sprite SpriteFor(LibrarySection section) noexcept {
  switch (section) {
    case LibrarySection::kMedia:
      return Sprite::kMedia;
    case LibrarySection::kDownloads:
      return Sprite::kDownloads;
    case LibrarySection::kBoosts:
      return Sprite::kBoosts;
    case LibrarySection::kSpaces:
      return Sprite::kSpaces;
    case LibrarySection::kRouting:
      return Sprite::kRouting;
    case LibrarySection::kHistory:
      break;
  }
  return Sprite::kHistory;
}

}  // namespace

LibraryPanel::LibraryPanel(HWND parent,
                           ViewContext& context,
                           xaml::UIElement chrome)
    : parent_(parent), context_(context), chrome_(std::move(chrome)) {}

LibraryPanel::~LibraryPanel() {
  input_target_.Reset();
  lifetime_.reset();
  ResetBatch();
  Close();
  if (source_) {
    try {
      source_.Content(nullptr);
      source_.Close();
    } catch (...) {
      platform::WriteDefaultLog(LogLevel::kWarning,
                                "Library island could not close");
    }
  }
}

void LibraryPanel::Build() {
  if (source_)
    return;
  root_ = controls::Grid();
  slider_ = controls::Grid();
  context_.brushes.Install(root_);
  backdrop_ = std::make_unique<ThemeBackdrop>();
  slider_.Children().Append(backdrop_->Root());
  AddColumn(slider_, kLibrary.side_width, xaml::GridUnitType::Pixel);
  AddColumn(slider_, 1, xaml::GridUnitType::Star);
  controls::Grid::SetColumnSpan(backdrop_->Root(), 2);

  // The rail: tabs centred between an empty header and the footer.
  side_ = controls::Grid();
  AddRow(side_, 0, xaml::GridUnitType::Auto);
  AddRow(side_, 1, xaml::GridUnitType::Star);
  AddRow(side_, 0, xaml::GridUnitType::Auto);
  AddRow(side_, 1, xaml::GridUnitType::Star);
  AddRow(side_, 0, xaml::GridUnitType::Auto);
  side_shade_ = controls::Border();
  side_shade_.IsHitTestVisible(false);
  controls::Grid::SetRowSpan(side_shade_, 5);
  side_.Children().Append(side_shade_);
  rail_ = controls::StackPanel();
  rail_.Spacing(kLibrary.tab_margin);
  controls::Grid::SetRow(rail_, 2);
  side_.Children().Append(rail_);
  constexpr std::array kOrder{
      LibrarySection::kRouting,   LibrarySection::kMedia,
      LibrarySection::kDownloads, LibrarySection::kBoosts,
      LibrarySection::kSpaces,    LibrarySection::kHistory};
  for (std::size_t index = 0; index < kOrder.size(); ++index) {
    auto& tab = tabs_[index];
    tab.section = kOrder[index];
    tab.button = controls::Button();
    tab.button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    tab.button.HorizontalContentAlignment(xaml::HorizontalAlignment::Center);
    tab.button.Margin({kLibrary.tab_margin, 0, kLibrary.tab_margin, 0});
    tab.button.Padding(
        {0, kLibrary.tab_padding_block, 0, kLibrary.tab_padding_block});
    tab.button.CornerRadius(UniformCorners(kLibrary.tab_radius));
    tab.button.BorderThickness(UniformThickness(0));
    controls::StackPanel content;
    content.Spacing(kLibrary.tab_gap);
    tab.icon = std::make_unique<SpriteIcon>(SpriteFor(tab.section),
                                            kLibrary.tab_icon_size);
    tab.icon->Root().HorizontalAlignment(xaml::HorizontalAlignment::Center);
    content.Children().Append(tab.icon->Root());
    tab.label = Label(L"", kTokens.type.size_workspace);
    tab.label.FontWeight(kSemiBold);
    tab.label.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    tab.label.Foreground(context_.brushes.text);
    content.Children().Append(tab.label);
    tab.button.Content(content);
    tab.click = tab.button.Click(
        winrt::auto_revoke, [this, section = tab.section](auto&&, auto&&) {
          context_.delegate.OnLibrarySectionSelected(section);
        });
    rail_.Children().Append(tab.button);
  }
  controls::Grid footer;
  footer.Padding(UniformThickness(kLibrary.footer_padding));
  back_ = PackButton(context_.icons, context_.brushes, IconRole::kBack, L"",
                     [this] { context_.delegate.OnLibraryDismissed(); });
  back_->control.HorizontalAlignment(xaml::HorizontalAlignment::Left);
  footer.Children().Append(back_->control);
  controls::Grid::SetRow(footer, 4);
  side_.Children().Append(footer);
  slider_.Children().Append(side_);

  // The open section.
  content_ = controls::Grid();
  AddRow(content_, 0, xaml::GridUnitType::Auto);
  AddRow(content_, 0, xaml::GridUnitType::Auto);
  AddRow(content_, 0, xaml::GridUnitType::Auto);
  AddRow(content_, 1, xaml::GridUnitType::Star);
  content_.MaxWidth(kLibrary.section_max_width);
  heading_ = Label(L"", kLibrary.title_size);
  heading_.FontWeight(kSemiBold);
  heading_.Foreground(context_.brushes.text);
  heading_.Margin({kLibrary.header_padding_inline, kLibrary.header_padding_top,
                   kLibrary.header_padding_inline,
                   kLibrary.header_padding_bottom});
  content_.Children().Append(heading_);
  actions_ = controls::Grid();
  actions_.ColumnSpacing(kLibrary.row_gap);
  actions_.Margin({kLibrary.header_padding_inline, 0,
                   kLibrary.header_padding_inline,
                   kLibrary.header_padding_bottom});
  AddColumn(actions_, 1, xaml::GridUnitType::Star);
  AddColumn(actions_, 0, xaml::GridUnitType::Auto);
  search_ = controls::TextBox();
  search_.MinWidth(0);
  search_.MaxLength(2048);
  actions_.Children().Append(search_);
  create_ = controls::Button();
  controls::Grid::SetColumn(create_, 1);
  actions_.Children().Append(create_);
  searched_ = search_.TextChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
    if (!updating_search_)
      context_.delegate.OnLibrarySearchChanged(
          winrt::to_string(search_.Text()));
  });
  created_ = create_.Click(winrt::auto_revoke, [this](auto&&, auto&&) {
    if (shown_section_)
      context_.delegate.OnLibraryCommand(*shown_section_, {},
                                         LibraryCommand::kCreate);
  });
  controls::Grid::SetRow(actions_, 1);
  content_.Children().Append(actions_);
  body_ = controls::Grid();
  controls::Grid::SetRow(body_, 3);
  empty_ = Label(L"", kLibrary.empty_size);
  empty_.Opacity(kLibrary.empty_opacity);
  empty_.Foreground(context_.brushes.text);
  empty_.TextWrapping(xaml::TextWrapping::Wrap);
  empty_.TextAlignment(xaml::TextAlignment::Center);
  empty_.Margin(UniformThickness(kLibrary.header_padding_inline));
  empty_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  empty_.VerticalAlignment(xaml::VerticalAlignment::Center);
  body_.Children().Append(empty_);
  content_.Children().Append(body_);
  controls::Grid::SetColumn(content_, 1);
  slider_.Children().Append(content_);
  root_.Children().Append(slider_);
  body_sized_ = body_.SizeChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
    LayoutSkeleton();
    if (tiles_)
      tiles_->SetWidth(body_.ActualWidth());
  });
  key_ = root_.KeyDown(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::KeyRoutedEventArgs& args) {
        if (args.Handled() ||
            args.Key() != winrt::Windows::System::VirtualKey::Escape)
          return;
        if (routing_form_ && routing_form_->IsOpen())
          context_.delegate.OnRoutingCancelled();
        else
          context_.delegate.OnLibraryDismissed();
        args.Handled(true);
      });
  fallback_key_ = root_.ProcessKeyboardAccelerators(
      winrt::auto_revoke,
      [this](auto&&,
             const xaml::Input::ProcessKeyboardAcceleratorEventArgs& args) {
        if (!args.Handled() && context_.input.ForwardUnhandledKey(
                                   static_cast<std::uint32_t>(args.Key())))
          args.Handled(true);
      });

  source_ = xaml::Hosting::DesktopWindowXamlSource();
  const auto island_parent = input_target_.IslandParent(parent_);
  winrt::check_bool(island_parent.has_value());
  source_.Initialize(
      winrt::Microsoft::UI::GetWindowIdFromWindow(*island_parent));
  source_.ShouldConstrainPopupsToWorkArea(true);
  source_.Content(root_);
  winrt::check_bool(input_target_
                        .Initialize(winrt::Microsoft::UI::GetWindowFromWindowId(
                            source_.SiteBridge().WindowId()))
                        .has_value());
}

void LibraryPanel::BuildOptIn() {
  if (opt_in_)
    return;
  opt_in_ = controls::Grid();
  skeleton_ = controls::Canvas();
  skeleton_.IsHitTestVisible(false);
  skeleton_.Opacity(kLibrary.skeleton_opacity);
  opt_in_.Children().Append(skeleton_);

  opt_in_card_ = controls::Border();
  opt_in_card_.Margin({kLibrary.opt_in_inset, 0, kLibrary.opt_in_inset, 0});
  opt_in_card_.Padding(
      {kLibrary.opt_in_padding_inline, kLibrary.opt_in_padding_top,
       kLibrary.opt_in_padding_inline, kLibrary.opt_in_padding_bottom});
  opt_in_card_.CornerRadius(UniformCorners(kLibrary.opt_in_radius));
  opt_in_card_.BorderThickness(UniformThickness(1));
  opt_in_card_.VerticalAlignment(xaml::VerticalAlignment::Center);
  controls::StackPanel card;
  opt_in_icon_ =
      std::make_unique<SpriteIcon>(Sprite::kMedia, kLibrary.opt_in_icon_size);
  opt_in_icon_->Root().HorizontalAlignment(xaml::HorizontalAlignment::Center);
  opt_in_icon_->Root().Margin({0, 2, 0, kLibrary.opt_in_icon_gap});
  card.Children().Append(opt_in_icon_->Root());
  opt_in_title_ = Label(L"", kLibrary.opt_in_title_size);
  opt_in_title_.FontWeight(winrt::Windows::UI::Text::FontWeight{700});
  opt_in_title_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  opt_in_title_.Margin({0, 0, 0, kLibrary.opt_in_title_gap});
  opt_in_title_.Foreground(context_.brushes.text);
  card.Children().Append(opt_in_title_);
  opt_in_body_ = Label(L"", kLibrary.opt_in_body_size);
  opt_in_body_.TextWrapping(xaml::TextWrapping::Wrap);
  opt_in_body_.TextAlignment(xaml::TextAlignment::Center);
  opt_in_body_.LineHeight(kLibrary.opt_in_body_size *
                          kLibrary.opt_in_body_line_height);
  opt_in_body_.Opacity(kLibrary.opt_in_body_opacity);
  opt_in_body_.Foreground(context_.brushes.text);
  opt_in_body_.Margin({0, 0, 0, kLibrary.opt_in_body_gap});
  card.Children().Append(opt_in_body_);

  opt_in_button_ = controls::Button();
  opt_in_button_.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  opt_in_button_.CornerRadius(UniformCorners(kLibrary.accent_radius));
  opt_in_button_.Padding(
      {kLibrary.accent_padding_inline, kLibrary.accent_padding_block,
       kLibrary.accent_padding_inline, kLibrary.accent_padding_block});
  opt_in_button_.BorderThickness(UniformThickness(0));
  opt_in_button_.FontSize(kLibrary.accent_text_size);
  opt_in_button_.FontWeight(kSemiBold);
  opt_in_button_.ScaleTransition(xaml::Vector3Transition());
  opt_in_button_.ScaleTransition().Duration(Ms(kLibrary.accent_hover_ms));
  opt_in_entered_ =
      opt_in_button_.PointerEntered(winrt::auto_revoke, [this](auto&&, auto&&) {
        if (!context_.motion.Enabled(context_.brushes.high_contrast()))
          return;
        opt_in_button_.CenterPoint(
            {static_cast<float>(opt_in_button_.ActualWidth()) / 2,
             static_cast<float>(opt_in_button_.ActualHeight()) / 2, 0});
        opt_in_button_.Scale(
            {kLibrary.accent_hover_scale, kLibrary.accent_hover_scale, 1});
      });
  opt_in_exited_ = opt_in_button_.PointerExited(
      winrt::auto_revoke,
      [this](auto&&, auto&&) { opt_in_button_.Scale({1, 1, 1}); });
  opt_in_click_ =
      opt_in_button_.Click(winrt::auto_revoke, [this](auto&&, auto&&) {
        context_.delegate.OnLibraryCommand(LibrarySection::kMedia, {},
                                           LibraryCommand::kEnable);
      });
  card.Children().Append(opt_in_button_);

  controls::StackPanel note;
  note.Orientation(controls::Orientation::Horizontal);
  note.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  note.Spacing(kLibrary.opt_in_note_gap);
  note.Margin({0, kLibrary.opt_in_note_margin, 0, 0});
  note.Opacity(kLibrary.opt_in_note_opacity);
  opt_in_lock_ = std::make_unique<IconView>(context_.icons, IconRole::kLock,
                                            kLibrary.opt_in_note_icon,
                                            context_.brushes.text);
  opt_in_lock_->Root().VerticalAlignment(xaml::VerticalAlignment::Top);
  opt_in_lock_->Root().Margin({0, 2, 0, 0});
  opt_in_note_ = Label(L"", kLibrary.opt_in_note_size);
  opt_in_note_.TextWrapping(xaml::TextWrapping::Wrap);
  opt_in_note_.TextTrimming(xaml::TextTrimming::None);
  opt_in_note_.Foreground(context_.brushes.text);
  opt_in_note_.MaxWidth(
      kLibrary.section_max_width -
      2 * (kLibrary.opt_in_inset + kLibrary.opt_in_padding_inline) -
      kLibrary.opt_in_note_icon - kLibrary.opt_in_note_gap);
  note.Children().Append(opt_in_lock_->Root());
  note.Children().Append(opt_in_note_);
  card.Children().Append(note);
  opt_in_card_.Child(card);
  opt_in_.Children().Append(opt_in_card_);
  body_.Children().Append(opt_in_);
}

// Zen's library-tile.svg mask: rounded tiles about 150px across, repeated to
// fill the section, inset 6/122 and rounded 16/122 of their cell.
void LibraryPanel::LayoutSkeleton() {
  if (!skeleton_)
    return;
  const double width = body_.ActualWidth(), height = body_.ActualHeight();
  if (width <= 0 || height <= 0)
    return;
  const double columns =
      std::max(1.0, std::round(width / kLibrary.skeleton_tile));
  const double cell = width / columns;
  const auto count =
      static_cast<std::uint32_t>(columns * std::ceil(height / cell));
  const auto children = skeleton_.Children();
  while (children.Size() < count) {
    controls::Border tile;
    tile.Background(context_.brushes.pill);
    children.Append(tile);
  }
  while (children.Size() > count)
    children.RemoveAtEnd();
  const double inset = cell * 6 / 122, size = cell - 2 * inset;
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto tile = children.GetAt(index).as<controls::Border>();
    tile.Width(size);
    tile.Height(size);
    tile.CornerRadius(UniformCorners(cell * 16 / 122));
    controls::Canvas::SetLeft(
        tile, (index % static_cast<std::uint32_t>(columns)) * cell + inset);
    controls::Canvas::SetTop(
        tile, (index / static_cast<std::uint32_t>(columns)) * cell + inset);
  }
}

Rect LibraryPanel::Anchor(const xaml::FrameworkElement& element) const {
  const auto rect = element.TransformToVisual(root_).TransformBounds(
      {0, 0, static_cast<float>(element.ActualWidth()),
       static_cast<float>(element.ActualHeight())});
  return {rect.X + bounds_.x, rect.Y + bounds_.y, rect.Width, rect.Height};
}

void LibraryPanel::Place(const ShellViewModel& vm) {
  const auto& layout = vm.layout;
  const auto& client = vm.client;
  const float width =
      std::min(kLibrary.min_width, std::max(1.0f, client.width));
  // Keep clear of the native caption buttons when they share the side.
  const float top = layout.sidebar_on_right && layout.native_titlebar.enabled
                        ? layout.native_titlebar.height
                        : 0.0f;
  const Rect bounds{layout.sidebar_on_right ? client.right() - width : client.x,
                    client.y + top, width, std::max(1.0f, client.height - top)};
  if (on_right_ != layout.sidebar_on_right) {
    on_right_ = layout.sidebar_on_right;
    // The rail stays on the sidebar's side.
    slider_.ColumnDefinitions().GetAt(0).Width(
        on_right_
            ? xaml::GridLength{1, xaml::GridUnitType::Star}
            : xaml::GridLength{kLibrary.side_width, xaml::GridUnitType::Pixel});
    slider_.ColumnDefinitions().GetAt(1).Width(
        on_right_
            ? xaml::GridLength{kLibrary.side_width, xaml::GridUnitType::Pixel}
            : xaml::GridLength{1, xaml::GridUnitType::Star});
    controls::Grid::SetColumn(side_, on_right_ ? 1 : 0);
    controls::Grid::SetColumn(content_, on_right_ ? 0 : 1);
    back_->icon->SetRole(on_right_ ? IconRole::kForward : IconRole::kBack);
    side_shade_.Background(nullptr);
  }
  if (!side_shade_.Background()) {
    // Zen's side: linear-gradient(to right, transparent, white 8%).
    media::LinearGradientBrush shade;
    shade.StartPoint({on_right_ ? 1.0f : 0.0f, 0});
    shade.EndPoint({on_right_ ? 0.0f : 1.0f, 0});
    media::GradientStop clear, light;
    clear.Color(NativeColor(Color{0x00FFFFFFu}));
    light.Color(
        NativeColor(Color{0xFFFFFFFFu}.WithAlpha(static_cast<std::uint8_t>(
            std::lround(kLibrary.side_highlight_alpha * 255)))));
    light.Offset(1);
    shade.GradientStops().Append(clear);
    shade.GradientStops().Append(light);
    side_shade_.Background(shade);
  }
  if (bounds == bounds_ && scale_ == vm.scale_factor)
    return;
  content_.Background(context_.glass.For(content_, GlassRole::kPanel,
                                         context_.brushes.transparent,
                                         kTokens.radius.card));
  context_.glass.Place(
      content_, {bounds.x + (on_right_ ? 0 : kLibrary.side_width), bounds.y,
                 bounds.width - kLibrary.side_width, bounds.height});
  bounds_ = bounds;
  scale_ = vm.scale_factor;
  const auto px = [&](float value) {
    return static_cast<std::int32_t>(std::lround(value * scale_));
  };
  const auto left = px(bounds.x), top_px = px(bounds.y);
  source_.SiteBridge().MoveAndResize(
      {left, top_px, std::max(1, px(bounds.right()) - left),
       std::max(1, px(bounds.bottom()) - top_px)});
}

void LibraryPanel::RenderRail(const ShellViewModel& vm) {
  const auto& library = vm.library;
  const auto& palette = library.palette;
  const bool labels = vm.client.height >= kLibrary.labels_min_height;
  const auto hover = context_.brushes.For(palette.tab_hover);
  const auto active = context_.brushes.For(palette.tab_active);
  for (auto& tab : tabs_) {
    const bool shown = std::ranges::find(library.sections, tab.section) !=
                       library.sections.end();
    Show(tab.button, shown);
    const bool selected = tab.section == library.section;
    tab.button.Background(selected ? active : context_.brushes.transparent);
    const auto resources = tab.button.Resources();
    resources.Insert(winrt::box_value(L"ButtonBackgroundPointerOver"),
                     selected ? active : hover);
    resources.Insert(winrt::box_value(L"ButtonBackgroundPressed"), active);
    // Zen fills the selected tab's sprite, and the downloads one while a
    // download is coming in.
    const bool filled =
        selected ||
        (tab.section == LibrarySection::kDownloads && library.downloading);
    if (tab.icon)
      tab.icon->SetColors(palette.icon, filled ? palette.icon_fill : Color{},
                          vm.scale_factor);
    const auto name = NativeText(LibrarySectionText(tab.section), vm.text);
    if (tab.label.Text() != name) {
      tab.label.Text(name);
      Accessible(tab.button, name);
    }
    Show(tab.label, labels);
  }
  SetButtonText(*back_, NativeText(UiText::kLibraryClose, vm.text));
}

void LibraryPanel::RenderSection(const ShellViewModel& vm) {
  const auto& library = vm.library;
  const bool switched = shown_section_ != library.section;
  if (switched && shown_section_ && open_) {
    // Zen replays the newly chosen tab's sprite and fades the section in.
    for (auto& tab : tabs_)
      if (tab.section == library.section && tab.icon &&
          context_.motion.Enabled(context_.brushes.high_contrast()))
        tab.icon->Play(0, design::sprites::kFrames - 1, kLibrary.sprite_ms,
                       false);
    fade_.Play(body_, context_.motion, context_.brushes.high_contrast(),
               kLibrary.section_fade_ms);
  }
  shown_section_ = library.section;
  heading_.Text(NativeText(LibrarySectionText(library.section), vm.text));
  const bool routing = library.section == LibrarySection::kRouting;
  const bool editing_route = routing && library.open &&
                             library.routing_editor &&
                             library.routing_editor->open;
  if (editing_route && !routing_form_) {
    routing_form_ = std::make_unique<RoutingForm>(context_);
    controls::Grid::SetRow(routing_form_->Root(), 2);
    content_.Children().Append(routing_form_->Root());
  }
  if (routing_form_) {
    if (editing_route)
      routing_form_->Render(*library.routing_editor, vm);
    else
      routing_form_->Close();
    Show(routing_form_->Root(), editing_route);
  }
  Show(actions_, !editing_route &&
                     (routing || library.section == LibrarySection::kBoosts));
  Show(search_, routing);
  search_.PlaceholderText(NativeText(UiText::kLibrarySearchRoutes, vm.text));
  const auto query = winrt::to_hstring(library.routing_query);
  if (search_.Text() != query) {
    updating_search_ = true;
    search_.Text(query);
    updating_search_ = false;
  }
  const auto create_label = NativeText(UiText::kLibraryNew, vm.text);
  const auto create_text = L"+ " + create_label;
  if (create_text_ != create_text) {
    create_text_ = create_text;
    create_.Content(winrt::box_value(create_text));
    Accessible(create_, create_label);
  }
  Show(heading_, !library.media_opt_in);

  const bool tiles = library.section == LibrarySection::kMedia &&
                     !library.media_opt_in && !library.tiles.empty();
  const bool rows =
      library.section != LibrarySection::kMedia && !library.rows.empty();
  if (tiles && !tiles_) {
    tiles_ = std::make_unique<LibraryList>(
        context_, true, [this](const xaml::FrameworkElement& element) {
          return Anchor(element);
        });
    tiles_->SetWidth(body_.ActualWidth());
    body_.Children().Append(tiles_->Root());
  }
  if (rows && !rows_) {
    rows_ = std::make_unique<LibraryList>(
        context_, false, [this](const xaml::FrameworkElement& element) {
          return Anchor(element);
        });
    body_.Children().Append(rows_->Root());
  }
  if (tiles_) {
    Show(tiles_->Root(), tiles);
    if (tiles)
      tiles_->Render(library.tiles, library.section, library.palette, vm.images,
                     vm.text);
  }
  if (rows_) {
    Show(rows_->Root(), rows);
    if (rows)
      rows_->Render(library.rows, library.section, library.palette, vm.images,
                    vm.text);
  }
  Show(empty_, library.empty != UiText::kCount);
  if (library.empty != UiText::kCount)
    empty_.Text(NativeText(library.empty, vm.text));

  if (library.media_opt_in)
    BuildOptIn();
  if (opt_in_) {
    const bool motion =
        context_.motion.Enabled(context_.brushes.high_contrast());
    opt_in_button_.ScaleTransition().Duration(
        Ms(motion ? kLibrary.accent_hover_ms : 0));
    if (!motion)
      opt_in_button_.Scale({1, 1, 1});
    Show(opt_in_, library.media_opt_in);
    const auto& palette = library.palette;
    opt_in_card_.Background(context_.brushes.For(palette.opt_in));
    opt_in_card_.BorderBrush(context_.brushes.For(palette.opt_in_border));
    opt_in_icon_->SetColors(palette.icon, palette.icon_fill, vm.scale_factor);
    const auto accent = context_.brushes.For(palette.accent);
    const auto accent_hover =
        context_.brushes.For(Mix(palette.accent, Color{0xFFFFFFFFu}, 0.1f));
    const auto white = context_.brushes.For(palette.accent_text);
    opt_in_button_.Background(accent);
    opt_in_button_.Foreground(white);
    const auto resources = opt_in_button_.Resources();
    resources.Insert(winrt::box_value(L"ButtonBackgroundPointerOver"),
                     accent_hover);
    resources.Insert(winrt::box_value(L"ButtonBackgroundPressed"), accent);
    resources.Insert(winrt::box_value(L"ButtonForegroundPointerOver"), white);
    resources.Insert(winrt::box_value(L"ButtonForegroundPressed"),
                     context_.brushes.For(palette.accent_text.WithAlpha(0xCC)));
    opt_in_title_.Text(NativeText(UiText::kLibraryMediaOptInTitle, vm.text));
    opt_in_body_.Text(NativeText(UiText::kLibraryMediaOptInBody, vm.text));
    opt_in_button_.Content(winrt::box_value(
        NativeText(UiText::kLibraryMediaOptInButton, vm.text)));
    opt_in_note_.Text(NativeText(UiText::kLibraryMediaOptInNote, vm.text));
    const bool pulse =
        library.media_opt_in && open_ &&
        context_.motion.Enabled(context_.brushes.high_contrast());
    if (pulse != pulsing_) {
      pulsing_ = pulse;
      if (pulse) {
        // Zen's zen-library-media-wait: 1 -> 0.45 -> 1 over 1.8s, endlessly,
        // and only while the note is shown.
        if (!pulse_) {
          const auto compositor =
              xaml::Hosting::ElementCompositionPreview::GetElementVisual(
                  skeleton_)
                  .Compositor();
          const auto ease = compositor.CreateCubicBezierEasingFunction(
              {0.42f, 0}, {0.58f, 1});
          pulse_ = compositor.CreateScalarKeyFrameAnimation();
          pulse_.Target(L"Opacity");
          const float rest = kLibrary.skeleton_opacity;
          pulse_.InsertKeyFrame(0, rest);
          pulse_.InsertKeyFrame(0.5f, rest * kLibrary.skeleton_pulse_low, ease);
          pulse_.InsertKeyFrame(1, rest, ease);
          pulse_.Duration(Ms(kLibrary.skeleton_pulse_ms));
          pulse_.IterationBehavior(
              composition::AnimationIterationBehavior::Forever);
        }
        skeleton_.StartAnimation(pulse_);
      } else {
        skeleton_.StopAnimation(pulse_);
      }
    }
  }
}

void LibraryPanel::Slide(bool open) {
  const float hidden = on_right_ ? bounds_.width : -bounds_.width;
  const auto bridge = source_.SiteBridge();
  const HWND window =
      winrt::Microsoft::UI::GetWindowFromWindowId(bridge.WindowId());
  ++serial_;
  ResetBatch();
  if (open && !visible_) {
    const HWND focus = GetFocus();
    return_focus_ =
        focus != window && !IsChild(window, focus) ? focus : nullptr;
    bridge.Show();
    visible_ = true;
  }
  // The island stays up while it slides away; keys must leave it now.
  if (!open) {
    if (pulse_ && skeleton_)
      skeleton_.StopAnimation(pulse_);
    pulsing_ = false;
    ReleaseFocus(window, std::exchange(return_focus_, nullptr), parent_);
  }
  if (open) {
    input_target_.Raise();
    bridge.MoveInZOrderAtTop();
  }
  const bool motion = context_.motion.Enabled(context_.brushes.high_contrast());
  if (!motion) {
    if (slide_)
      slider_.StopAnimation(slide_);
    if (chrome_fade_)
      chrome_.StopAnimation(chrome_fade_);
    slider_.Translation({open ? 0.0f : hidden, 0, 0});
    chrome_.Opacity(open ? 0.0f : 1.0f);
    chrome_opacity_ = open ? 0.0f : 1.0f;
    if (!open) {
      HideSource();
      visible_ = false;
      context_.hover_changed();
    }
    return;
  }
  const auto compositor =
      xaml::Hosting::ElementCompositionPreview::GetElementVisual(slider_)
          .Compositor();
  if (!slide_)
    slide_ = compositor.CreateSpringVector3Animation();
  const auto& spring = slide_;
  spring.Target(L"Translation");
  spring.InitialValue(nullptr);
  if (open && slider_.Translation().x == 0 && chrome_opacity_ == 1)
    spring.InitialValue(
        winrt::Windows::Foundation::Numerics::float3{hidden, 0, 0});
  spring.FinalValue(
      winrt::Windows::Foundation::Numerics::float3{open ? 0.0f : hidden, 0, 0});
  spring.DampingRatio(kLibrary.spring_damping_ratio);
  spring.Period(Ms(kLibrary.spring_period_ms));
  batch_ = compositor.CreateScopedBatch(
      composition::CompositionBatchTypes::Animation);
  slider_.StartAnimation(spring);
  // Zen fades and slightly shrinks the toolbox in the first third of the
  // library's travel.
  if (!chrome_fade_)
    chrome_fade_ = compositor.CreateScalarKeyFrameAnimation();
  const auto& fade = chrome_fade_;
  fade.Target(L"Opacity");
  fade.InsertKeyFrame(0, chrome_opacity_);
  fade.InsertKeyFrame(1, open ? 0.0f : 1.0f);
  fade.Duration(Ms(kLibrary.spring_period_ms / 2));
  chrome_opacity_ = open ? 0.0f : 1.0f;
  chrome_.StartAnimation(fade);
  batch_.End();
  batch_done_ = batch_.Completed([this, weak = std::weak_ptr<int>(lifetime_),
                                  serial = serial_](auto&&, auto&&) {
    if (weak.expired() || serial != serial_ || open_)
      return;
    // Off-screen now; the native island need not stay up.
    HideSource();
    visible_ = false;
    context_.hover_changed();
  });
}

void LibraryPanel::HideSource() {
  const auto bridge = source_.SiteBridge();
  hide_.Hide(winrt::Microsoft::UI::GetWindowFromWindowId(bridge.WindowId()),
             root_.DispatcherQueue(), [bridge] { bridge.Hide(); });
}

void LibraryPanel::ResetBatch() noexcept {
  // Composition batches do not support the weak references used by
  // auto_revoke. Keep the source alive until its handler has been removed.
  if (const auto batch = std::exchange(batch_, nullptr))
    batch.Completed(std::exchange(batch_done_, {}));
}

void LibraryPanel::Render(const ShellViewModel& vm, xaml::ElementTheme theme) {
  const auto& library = vm.library;
  if (!library.open && !visible_) {
    open_ = false;
    return;
  }
  Build();
  if (library.open)
    hide_.BeforeShow();
  if (root_.RequestedTheme() != theme)
    root_.RequestedTheme(theme);
  Place(vm);
  backdrop_->Render(vm.theme, vm.client, bounds_,
                    context_.brushes.high_contrast());
  RenderRail(vm);
  const bool was_open = open_;
  if (library.open && !was_open)
    shown_section_.reset();
  open_ = library.open;
  RenderSection(vm);
  if (open_ == was_open)
    return;
  Slide(open_);
  if (open_)
    for (auto& tab : tabs_)
      if (tab.section == library.section)
        tab.button.Focus(xaml::FocusState::Programmatic);
}

void LibraryPanel::Raise() noexcept try {
  if (source_ && visible_) {
    input_target_.Raise();
    source_.SiteBridge().MoveInZOrderAtTop();
  }
} catch (...) {
  platform::WriteDefaultLog(LogLevel::kWarning,
                            "Library island could not be raised");
}

bool LibraryPanel::Contains(Point point) const noexcept {
  if (!OwnsInput())
    return false;
  if (bounds_.Contains(point))
    return true;
  try {
    // A child flyout is part of this interaction even outside the panel frame.
    if (root_ && root_.XamlRoot())
      for (const auto& popup :
           media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root_.XamlRoot()))
        if (popup.IsOpen())
          if (const auto child = popup.Child().try_as<xaml::FrameworkElement>();
              child && context_.pointer_over(child))
            return true;
  } catch (...) {
  }
  return false;
}

void LibraryPanel::Close() noexcept try {
  const bool was_visible = visible_;
  open_ = false;
  if (routing_form_)
    routing_form_->Close();
  ++serial_;
  ResetBatch();
  if (slide_)
    slider_.StopAnimation(slide_);
  if (chrome_fade_)
    chrome_.StopAnimation(chrome_fade_);
  if (pulse_ && skeleton_)
    skeleton_.StopAnimation(pulse_);
  pulsing_ = false;
  if (chrome_)
    chrome_.Opacity(1);
  chrome_opacity_ = 1;
  if (source_ && visible_) {
    ReleaseFocus(winrt::Microsoft::UI::GetWindowFromWindowId(
                     source_.SiteBridge().WindowId()),
                 return_focus_, parent_);
    HideSource();
  }
  return_focus_ = nullptr;
  visible_ = false;
  if (was_visible)
    context_.hover_changed();
} catch (...) {
  platform::WriteDefaultLog(LogLevel::kWarning,
                            "Library island could not hide");
}
}  // namespace kr::win
