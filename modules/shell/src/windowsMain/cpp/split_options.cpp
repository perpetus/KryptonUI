#include "split_options.hpp"

#include <algorithm>
#include <utility>

#include "krypton/ui/layout/popup_layout.hpp"
#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
SplitOptions::SplitOptions(HWND parent, ViewContext& context)
    : context_(context), island_(parent) {}
SplitOptions::~SplitOptions() {
  Close();
}

void SplitOptions::Build() {
  if (popup_)
    return;
  island_.Ensure();
  context_.brushes.Install(island_.Root());
  popup_ = controls::Primitives::Popup();
  popup_.IsLightDismissEnabled(true);
  popup_.ShouldConstrainToRootBounds(true);
  surface_ = controls::Border();
  surface_.Width(kTokens.split_view.options_width);
  surface_.Padding(UniformThickness(kTokens.split_view.options_padding));
  surface_.CornerRadius(UniformCorners(kTokens.radius.overlay));
  surface_.BorderThickness(UniformThickness(kTokens.overlay.border_width));
  controls::StackPanel content;
  content.Spacing(kTokens.split_view.snap_gap);
  controls::Grid layouts;
  layouts.ColumnSpacing(kTokens.split_view.snap_gap);
  layouts.RowSpacing(kTokens.split_view.snap_gap);
  const auto columns =
      std::max(std::size_t{1},
               static_cast<std::size_t>(kTokens.split_view.options_columns));
  for (std::size_t i = 0; i < columns; ++i)
    AddColumn(layouts, 1, xaml::GridUnitType::Star);
  for (std::size_t i = 0; i < (tiles_.size() + columns - 1) / columns; ++i)
    AddRow(layouts, 0, xaml::GridUnitType::Auto);
  for (std::size_t i = 0; i < tiles_.size(); ++i) {
    auto& tile = tiles_[i];
    tile.button = controls::Button();
    tile.button.Padding(UniformThickness(kTokens.split_view.snap_tile_padding));
    tile.button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    tile.button.CornerRadius(UniformCorners(kTokens.radius.button));
    controls::Canvas canvas;
    canvas.Width(kTokens.split_view.snap_preview_width);
    canvas.Height(kTokens.split_view.snap_preview_height);
    canvas.IsHitTestVisible(false);
    canvas.FlowDirection(xaml::FlowDirection::LeftToRight);
    for (std::size_t p = 0; p < kMaxSplitMembers; ++p) {
      tile.panes[p] = controls::Border();
      tile.panes[p].CornerRadius(
          UniformCorners(kTokens.split_view.snap_pane_radius));
      tile.panes[p].BorderThickness(
          UniformThickness(kTokens.split_view.snap_pane_stroke));
      tile.icons[p] = controls::Image();
      tile.icons[p].Width(kTokens.split_view.member_icon_size);
      tile.icons[p].Height(kTokens.split_view.member_icon_size);
      tile.icons[p].HorizontalAlignment(xaml::HorizontalAlignment::Center);
      tile.icons[p].VerticalAlignment(xaml::VerticalAlignment::Center);
      tile.panes[p].Child(tile.icons[p]);
      canvas.Children().Append(tile.panes[p]);
    }
    tile.button.Content(canvas);
    tile.click = tile.button.Click(winrt::auto_revoke, [this, i](auto&&,
                                                                 auto&&) {
      context_.delegate.OnSplitLayoutSelected(kSplitLayoutChoices[i].layout);
    });
    controls::Grid::SetColumn(tile.button, static_cast<int>(i % columns));
    controls::Grid::SetRow(tile.button, static_cast<int>(i / columns));
    layouts.Children().Append(tile.button);
  }
  content.Children().Append(layouts);
  for (auto& page : pages_) {
    page.row = controls::StackPanel();
    page.row.Orientation(controls::Orientation::Horizontal);
    page.row.Spacing(kTokens.split_view.member_gap);
    page.row.Height(kTokens.split_view.options_page_height);
    page.icon = controls::Image();
    page.icon.Width(kTokens.spacing.icon_size);
    page.icon.Height(kTokens.spacing.icon_size);
    page.icon.VerticalAlignment(xaml::VerticalAlignment::Center);
    page.title = controls::TextBlock();
    page.title.FontSize(kTokens.type.size_small);
    page.title.MaxWidth(kTokens.split_view.options_title_width);
    page.title.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
    page.title.VerticalAlignment(xaml::VerticalAlignment::Center);
    page.row.Children().Append(page.icon);
    page.row.Children().Append(page.title);
    content.Children().Append(page.row);
  }
  controls::ScrollViewer scroll;
  scroll.HorizontalScrollBarVisibility(controls::ScrollBarVisibility::Disabled);
  scroll.VerticalScrollBarVisibility(controls::ScrollBarVisibility::Auto);
  scroll.Content(content);
  surface_.Child(scroll);
  popup_.Child(surface_);
  island_.Root().Children().Append(popup_);
  loaded_ = island_.Root().Loaded(winrt::auto_revoke,
                                  [this](auto&&, auto&&) { Open(); });
  content_loaded_ =
      surface_.Loaded(winrt::auto_revoke, [this](auto&&, auto&&) { Open(); });
  size_ = surface_.SizeChanged(winrt::auto_revoke,
                               [this](auto&&, auto&&) { Place(); });
  closed_ = popup_.Closed(winrt::auto_revoke, [this](auto&&, auto&&) {
    const bool dismissed = std::exchange(requested_, false);
    Close();
    if (dismissed)
      context_.delegate.OnSplitChooserDismissed();
  });
}

void SplitOptions::Close() noexcept {
  if (!requested_ && !populated_)
    return;
  requested_ = false;
  focus_pending_ = false;
  populated_ = false;
  if (!popup_)
    return;
  try {
    context_.glass.Place(surface_, {});
    popup_.IsOpen(false);
    for (auto& tile : tiles_)
      for (auto& icon : tile.icons)
        icon.Source(nullptr);
    for (auto& page : pages_)
      page.icon.Source(nullptr);
    island_.Hide();
  } catch (...) {
  }
}
void SplitOptions::Open() noexcept {
  try {
    if (!requested_ || !island_.IsReady())
      return;
    if (!popup_.IsOpen())
      popup_.IsOpen(true);
    Place();
    if (focus_pending_ && tiles_[focused_].button.IsLoaded())
      focus_pending_ =
          !tiles_[focused_].button.Focus(xaml::FocusState::Programmatic);
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Could not open split options");
  }
}
void SplitOptions::Place() noexcept {
  if (!requested_ || placing_ || !island_.IsReady())
    return;
  placing_ = true;
  try {
    const auto client = island_.ClientBounds();
    const auto bounds =
        PlaceAnchoredPopup(client, anchor_,
                           {static_cast<float>(surface_.ActualWidth()),
                            static_cast<float>(surface_.ActualHeight())},
                           false);
    const auto height = PopupViewport(client).height;
    if (surface_.MaxHeight() != height)
      surface_.MaxHeight(height);
    if (popup_.HorizontalOffset() != bounds.x - client.x)
      popup_.HorizontalOffset(bounds.x - client.x);
    if (popup_.VerticalOffset() != bounds.y - client.y)
      popup_.VerticalOffset(bounds.y - client.y);
    context_.glass.Place(surface_, bounds);
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Could not position split options");
  }
  placing_ = false;
}
void SplitOptions::Render(const ShellViewModel::SplitChooserVm& model,
                          UiTextView text,
                          Rect client,
                          float scale,
                          xaml::ElementTheme theme,
                          const ImageStore* images) {
  Build();
  populated_ = true;
  focus_pending_ |= !requested_;
  requested_ = true;
  anchor_ = model.anchor;
  surface_.RequestedTheme(theme);
  surface_.BorderBrush(context_.brushes.search_border);
  surface_.Background(context_.glass.For(surface_, GlassRole::kMenu,
                                         context_.brushes.search_background,
                                         kTokens.radius.overlay));
  const auto candidate = [&](std::size_t index) -> const TabItemVm* {
    if (index >= model.selected.size())
      return nullptr;
    const auto it = std::ranges::find(model.candidates, model.selected[index],
                                      &TabItemVm::id);
    return it == model.candidates.end() ? nullptr : &*it;
  };
  for (std::size_t i = 0; i < tiles_.size(); ++i) {
    auto& tile = tiles_[i];
    const auto& choice = kSplitLayoutChoices[i];
    if (choice.layout == model.layout)
      focused_ = i;
    const bool supported =
        model.snap_layouts || !IsSnapSplitLayout(choice.layout);
    tile.button.Visibility(supported ? xaml::Visibility::Visible
                                     : xaml::Visibility::Collapsed);
    tile.button.Background(choice.layout == model.layout
                               ? context_.brushes.active
                               : context_.brushes.transparent);
    const auto label = NativeText(choice.label, text);
    Accessible(tile.button, label);
    controls::ToolTipService::SetToolTip(tile.button, winrt::box_value(label));
    const auto geometry = PresentSplitLayoutPreview(
        choice,
        {0, 0, kTokens.split_view.snap_preview_width,
         kTokens.split_view.snap_preview_height},
        kTokens.split_view.snap_gap, text.right_to_left);
    for (std::size_t p = 0; p < kMaxSplitMembers; ++p) {
      const auto& pane = tile.panes[p];
      pane.Visibility(p < geometry.pane_count ? xaml::Visibility::Visible
                                              : xaml::Visibility::Collapsed);
      if (p >= geometry.pane_count)
        continue;
      const auto bounds = geometry.panes[p];
      controls::Canvas::SetLeft(pane, bounds.x);
      controls::Canvas::SetTop(pane, bounds.y);
      pane.Width(bounds.width);
      pane.Height(bounds.height);
      pane.Background(context_.brushes.pill);
      pane.BorderBrush(context_.brushes.search_text);
      const auto* tab = candidate(p);
      const auto bitmap =
          tab ? context_.images.Get(tab->favicon, images) : nullptr;
      if (tile.icons[p].Source() != bitmap)
        tile.icons[p].Source(bitmap);
    }
  }
  for (std::size_t p = 0; p < pages_.size(); ++p) {
    auto& page = pages_[p];
    const auto* tab = candidate(p);
    page.row.Visibility(tab ? xaml::Visibility::Visible
                            : xaml::Visibility::Collapsed);
    page.title.Text(tab ? winrt::to_hstring(tab->title) : winrt::hstring{});
    page.title.Foreground(context_.brushes.text);
    const auto bitmap =
        tab ? context_.images.Get(tab->favicon, images) : nullptr;
    if (page.icon.Source() != bitmap)
      page.icon.Source(bitmap);
  }
  island_.Show(client, scale, theme);
  Open();
}
}  // namespace kr::win
