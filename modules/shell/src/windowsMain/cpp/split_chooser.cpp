#include "split_chooser.hpp"

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>

#include <algorithm>
#include <utility>

#include "split_options.hpp"

namespace kr::win {
SplitChooser::SplitChooser(HWND parent,
                           ViewContext& context,
                           std::move_only_function<void(bool)> focus_blocked)
    : context_(context),
      parent_(parent),
      island_(parent),
      focus_blocked_(std::move(focus_blocked)) {}
SplitChooser::~SplitChooser() {
  lifetime_.reset();
  if (dialog_ && showing_)
    dialog_.Hide();
}
void SplitChooser::Build() {
  if (dialog_)
    return;
  island_.Ensure();
  context_.brushes.Install(island_.Root());
  dialog_ = controls::ContentDialog();
  dialog_.DefaultButton(controls::ContentDialogButton::Primary);
  controls::StackPanel content;
  content.Spacing(kTokens.spacing.section_gap);
  content.MaxWidth(kTokens.split_view.chooser_width);
  layout_heading_ = controls::TextBlock();
  layouts_ = controls::Grid();
  const auto& snap = kTokens.split_view;
  const auto columns = static_cast<std::size_t>(snap.snap_columns);
  layouts_.ColumnSpacing(snap.snap_gap);
  layouts_.RowSpacing(snap.snap_gap);
  for (std::size_t i = 0; i < columns; ++i)
    layouts_.ColumnDefinitions().Append(controls::ColumnDefinition());
  for (std::size_t i = 0;
       i < (kSplitLayoutChoices.size() + columns - 1) / columns; ++i) {
    controls::RowDefinition row;
    row.Height(xaml::GridLengthHelper::Auto());
    layouts_.RowDefinitions().Append(row);
  }
  for (std::size_t i = 0; i < kSplitLayoutChoices.size(); ++i) {
    auto& button = layout_tiles_[i].button;
    button = controls::Primitives::ToggleButton();
    button.Padding(UniformThickness(snap.snap_tile_padding));
    button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    controls::Grid::SetColumn(button, static_cast<int>(i % columns));
    controls::Grid::SetRow(button, static_cast<int>(i / columns));
    auto& canvas = layout_tiles_[i].canvas;
    canvas = controls::Canvas();
    canvas.Width(snap.snap_preview_width);
    canvas.Height(snap.snap_preview_height);
    canvas.IsHitTestVisible(false);
    // Geometry is already mirrored by the shared presenter.
    canvas.FlowDirection(xaml::FlowDirection::LeftToRight);
    for (std::size_t p = 0; p < kMaxSplitMembers; ++p) {
      auto& pane = layout_tiles_[i].panes[p];
      pane = controls::Border();
      pane.CornerRadius(UniformCorners(snap.snap_pane_radius));
      pane.BorderThickness(UniformThickness(snap.snap_pane_stroke));
      auto& image = layout_tiles_[i].icons[p];
      image = controls::Image();
      image.Width(snap.member_icon_size);
      image.Height(snap.member_icon_size);
      image.HorizontalAlignment(xaml::HorizontalAlignment::Center);
      image.VerticalAlignment(xaml::VerticalAlignment::Center);
      pane.Child(image);
      canvas.Children().Append(pane);
    }
    button.Content(canvas);
    layout_tiles_[i].click =
        button.Click(winrt::auto_revoke, [this, i](auto&&, auto&&) {
          if (!applying_) {
            layout_tiles_[i].button.IsChecked(true);
            context_.delegate.OnSplitLayoutSelected(
                kSplitLayoutChoices[i].layout);
          }
        });
    layouts_.Children().Append(button);
  }
  tabs_ = controls::ListView();
  tabs_.SelectionMode(controls::ListViewSelectionMode::Multiple);
  tabs_.MaxHeight(kTokens.split_view.chooser_height);
  tabs_.MinWidth(kTokens.split_view.chooser_width);
  tabs_.ItemTemplate(
      xaml::Markup::XamlReader::Load(
          LR"(<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"><TextBlock TextTrimming="CharacterEllipsis" VerticalAlignment="Center" /></DataTemplate>)")
          .as<xaml::DataTemplate>());
  items_ = winrt::single_threaded_observable_vector<IInspectable>();
  tabs_.ItemsSource(items_);
  realizing_ = tabs_.ContainerContentChanging(
      winrt::auto_revoke,
      [this](auto&&, const controls::ContainerContentChangingEventArgs& args) {
        if (args.InRecycleQueue())
          return;
        const auto item = args.Item().try_as<foundation::IPropertyValue>();
        const auto root = args.ItemContainer()
                              .ContentTemplateRoot()
                              .try_as<controls::TextBlock>();
        if (!item || !root)
          return;
        if (const auto tab = context_.delegate.OnSplitCandidateRequested(
                TabId{item.GetUInt64()})) {
          root.Text(winrt::to_hstring(tab->title));
          root.FontSize(kTokens.type.size);
          root.Foreground(context_.brushes.text);
          args.ItemContainer().MinHeight(kTokens.split_view.chooser_row_height);
          args.Handled(true);
        }
      });
  selection_ =
      tabs_.SelectionChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
        if (applying_)
          return;
        const auto items = tabs_.SelectedItems();
        std::array<TabId, kMaxSplitMembers> ids{};
        const std::size_t count = items.Size();
        if (count <= ids.size()) {
          for (std::size_t i = 0; i < count; ++i)
            ids[i] = TabId{items.GetAt(static_cast<std::uint32_t>(i))
                               .as<foundation::IPropertyValue>()
                               .GetUInt64()};
          if (context_.delegate.OnSplitSelectionChanged(
                  std::span(ids).first(count))) {
            selected_ = ids;
            selected_count_ = count;
            dialog_.IsPrimaryButtonEnabled(count >= 2);
            return;
          }
        }
        RestoreSelection();
      });
  accepted_ = dialog_.PrimaryButtonClick(
      winrt::auto_revoke,
      [this](auto&&, const controls::ContentDialogButtonClickEventArgs& args) {
        if (!context_.delegate.OnSplitChooserAccepted())
          args.Cancel(true);
      });
  content.Children().Append(layout_heading_);
  content.Children().Append(layouts_);
  content.Children().Append(tabs_);
  dialog_.Content(content);
  loaded_ = island_.Root().Loaded(winrt::auto_revoke,
                                  [this](auto&&, auto&&) { TryOpen(); });
}
void SplitChooser::RestoreSelection() {
  const auto current = tabs_.SelectedItems();
  bool same = current.Size() == selected_count_;
  for (std::uint32_t i = 0; same && i < current.Size(); ++i) {
    const TabId id{
        current.GetAt(i).as<foundation::IPropertyValue>().GetUInt64()};
    const auto end = selected_.begin() + selected_count_;
    same = std::find(selected_.begin(), end, id) != end;
  }
  if (same)
    return;
  applying_ = true;
  const auto selected = tabs_.SelectedItems();
  selected.Clear();
  for (std::size_t i = 0; i < selected_count_; ++i) {
    const auto found = std::ranges::find(keys_, selected_[i]);
    if (found != keys_.end())
      selected.Append(
          items_.GetAt(static_cast<std::uint32_t>(found - keys_.begin())));
  }
  applying_ = false;
}
void SplitChooser::TryOpen() {
  if (!requested_ || showing_ || !island_.IsReady())
    return;
  dialog_.XamlRoot(island_.Root().XamlRoot());
  showing_ = true;
  const std::weak_ptr<int> weak = lifetime_;
  const auto generation = open_generation_;
  operation_ = dialog_.ShowAsync();
  const auto queue = dialog_.DispatcherQueue();
  operation_.Completed([this, weak, queue, generation](auto&&, auto&&) {
    if (weak.expired())
      return;
    queue.TryEnqueue([this, weak, generation] {
      if (weak.expired())
        return;
      showing_ = false;
      operation_ = nullptr;
      // A new request can arrive after Hide but before this completion runs.
      // Finish the old operation without dismissing the new chooser state.
      if (generation != open_generation_) {
        if (!requested_)
          focus_blocked_(options_ && options_->IsOpen());
        TryOpen();
        return;
      }
      requested_ = false;
      island_.Hide();
      focus_blocked_(false);
      context_.delegate.OnSplitChooserDismissed();
    });
  });
}
void SplitChooser::Render(const ShellViewModel::SplitChooserVm& model,
                          UiTextView text,
                          Rect client,
                          float scale,
                          xaml::ElementTheme theme,
                          const ImageStore* images) {
  if (options_ && (!model.open || !model.layout_only))
    options_->Close();
  if (model.open && model.layout_only) {
    if (dialog_ && showing_) {
      ++open_generation_;
      dialog_.Hide();
    }
    requested_ = false;
    island_.Hide();
    focus_blocked_(true);
    if (!options_)
      options_ = std::make_unique<SplitOptions>(parent_, context_);
    options_->Render(model, text, client, scale, theme, images);
    return;
  }
  if (!model.open) {
    if (!showing_)
      focus_blocked_(false);
    requested_ = false;
    if (dialog_ && showing_)
      dialog_.Hide();
    island_.Hide();
    return;
  }
  Build();
  if (!requested_)
    ++open_generation_;
  requested_ = true;
  island_.Root().FlowDirection(text.right_to_left
                                   ? xaml::FlowDirection::RightToLeft
                                   : xaml::FlowDirection::LeftToRight);
  applying_ = true;
  std::array<UiText, 4 + kSplitLayoutChoices.size()> text_ids{
      UiText::kSplitChooserTitle, UiText::kSplitCreate,
      UiText::kWorkspaceCancel, UiText::kSplitSnapLayouts};
  for (std::size_t i = 0; i < kSplitLayoutChoices.size(); ++i)
    text_ids[i + 4] = kSplitLayoutChoices[i].label;
  bool refresh = false;
  for (std::size_t i = 0; i < text_ids.size(); ++i)
    refresh |= labels_[i] != text[text_ids[i]];
  if (refresh) {
    for (std::size_t i = 0; i < text_ids.size(); ++i)
      labels_[i] = text[text_ids[i]];
    dialog_.Title(winrt::box_value(winrt::to_hstring(labels_[0])));
    dialog_.PrimaryButtonText(winrt::to_hstring(labels_[1]));
    dialog_.CloseButtonText(winrt::to_hstring(labels_[2]));
    layout_heading_.Text(winrt::to_hstring(labels_[3]));
    for (std::size_t i = 0; i < kSplitLayoutChoices.size(); ++i) {
      const auto label = winrt::to_hstring(labels_[i + 4]);
      xaml::Automation::AutomationProperties::SetName(layout_tiles_[i].button,
                                                      label);
      controls::ToolTipService::SetToolTip(layout_tiles_[i].button,
                                           winrt::box_value(label));
    }
  }
  if (dialog_.RequestedTheme() != theme)
    dialog_.RequestedTheme(theme);
  if (dialog_.FlowDirection() != island_.Root().FlowDirection())
    dialog_.FlowDirection(island_.Root().FlowDirection());
  layout_heading_.Foreground(context_.brushes.text);
  for (std::size_t i = 0; i < kSplitLayoutChoices.size(); ++i) {
    const auto& choice = kSplitLayoutChoices[i];
    const bool supported =
        model.snap_layouts || !IsSnapSplitLayout(choice.layout);
    layout_tiles_[i].button.Visibility(supported ? xaml::Visibility::Visible
                                                 : xaml::Visibility::Collapsed);
    layout_tiles_[i].button.IsChecked(choice.layout == model.layout);
    if (!supported)
      continue;
    const auto geometry = PresentSplitLayoutPreview(
        choice,
        {0, 0, kTokens.split_view.snap_preview_width,
         kTokens.split_view.snap_preview_height},
        kTokens.split_view.snap_gap, text.right_to_left);
    for (std::size_t p = 0; p < kMaxSplitMembers; ++p) {
      const auto pane = layout_tiles_[i].panes[p];
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
      pane.BorderBrush(context_.brushes.icon);
      const auto selected =
          p < model.selected.size() ? model.selected[p] : TabId{};
      const auto tab =
          std::ranges::find(model.candidates, selected, &TabItemVm::id);
      const auto bitmap = tab != model.candidates.end()
                              ? context_.images.Get(tab->favicon, images)
                              : nullptr;
      if (layout_tiles_[i].icons[p].Source() != bitmap)
        layout_tiles_[i].icons[p].Source(bitmap);
    }
  }
  if (keys_.size() != model.candidates.size() ||
      !std::ranges::equal(keys_, model.candidates, {}, {}, &TabItemVm::id)) {
    keys_.clear();
    items_.Clear();
    for (const auto& tab : model.candidates) {
      keys_.push_back(tab.id);
      items_.Append(winrt::box_value(tab.id.value));
    }
  }
  selected_count_ = model.selected.size();
  std::ranges::copy(model.selected, selected_.begin());
  RestoreSelection();
  applying_ = false;
  dialog_.IsPrimaryButtonEnabled(selected_count_ >= 2);
  focus_blocked_(true);
  island_.Show(client, scale, theme);
  TryOpen();
}
}  // namespace kr::win
