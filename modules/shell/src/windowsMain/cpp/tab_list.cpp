#include "tab_list.hpp"

#include <windows.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <format>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>

#include "icon_view.hpp"
#include "krypton/ui/core/env.hpp"
#include "krypton/ui/layout/horizontal_layout.hpp"
#include "krypton/ui/platform/platform.hpp"
#include "tab_preview_surface.hpp"

namespace kr::win {
bool TabList::FocusNative(TabId target) {
  const auto found = records_.find(target);
  if (found == records_.end())
    return false;
  tabs_.ScrollIntoView(found->second.item);
  const auto container =
      tabs_.ContainerFromItem(found->second.item).try_as<controls::Control>();
  return container && container.Focus(xaml::FocusState::Programmatic);
}
using namespace controls;
using winrt::Windows::System::VirtualKey;
using xaml::CornerRadius;
using xaml::Thickness;
using xaml::Visibility;
namespace hosting = xaml::Hosting;

TabList::TabList(ViewContext& context, bool& anchors_dirty)
    : context_(context), anchors_dirty_(anchors_dirty), favorites_(context) {
  BuildTabs();
}

TabList::~TabList() {
  lifetime_.reset();
  RemoveDrag();
}

void TabList::Render(const ShellViewModel& vm) {
  folder_theme_ = vm.theme;
  // Selection changes update the retained icon without reconciling the list.
  if (!rendered_ || vm.changed.Has(Dirty::kTabs) ||
      vm.changed.Has(Dirty::kActiveTab) || vm.changed.Has(Dirty::kTheme)) {
    for (const auto& value : vm.folder_rows) {
      if (!value.folder)
        continue;
      const auto found = folder_records_.find(value.folder);
      if (found == folder_records_.end())
        continue;
      auto& record = found->second;
      const bool changed = record.contains_active != value.contains_active;
      record.contains_active = value.contains_active;
      if (record.row && record.row->folder_icon &&
          ((changed && record.row->folder_collapsed) ||
           vm.changed.Has(Dirty::kTheme))) {
        record.row->folder_icon->Update(
            !record.row->folder_collapsed, record.contains_active,
            record.row->data.url, folder_theme_, context_.brushes,
            context_.motion, rendered_);
      }
    }
  }
  if (!rendered_ || vm.changed.Has(Dirty::kSpaces)) {
    folder_workspaces_.clear();
    for (const auto& space : vm.spaces)
      folder_workspaces_.emplace_back(space.id, winrt::to_hstring(space.name));
  }
  preview_metrics_ = MeasureTabPreviewForLayout(vm.client, vm.layout);
  if (!horizontal_)
    SetHoverWidth(vm.layout.sidebar.width);
  // A drag ends with the window's activation, and when the Essentials it
  // reorders change underneath it.
  if (drag_.phase != DragPhase::kIdle && !vm.header.window_active)
    CancelDrag();
  if (!horizontal_ && favorites_.Render(vm) &&
      drag_.phase == DragPhase::kDragging &&
      (drag_.source == DragSource::kEssential ||
       drag_.target.kind == DropTarget::Kind::kEssentials))
    CancelDrag();
  for (const auto& [key, row] : rows_) {
    row->preview->Configure(preview_metrics_);
    row->preview->Render(vm);
  }
  if (today_idle_ != vm.today_idle) {
    today_idle_ = vm.today_idle;
    UpdateClears();
  }
  if (rendered_ && !vm.changed.Has(Dirty::kTabs) &&
      !vm.changed.Has(Dirty::kActiveTab) && !vm.changed.Has(Dirty::kImages) &&
      !vm.changed.Has(Dirty::kMultiPage))
    return;
  if (rendered_ && !vm.changed.Has(Dirty::kTabs) &&
      !vm.changed.Has(Dirty::kImages)) {
    UpdateActiveTab(vm);
  } else {
    ReconcileTabs(vm);
    LayoutHorizontalRows();
  }
  if (horizontal_ && vm.layout.tab_strip_visible)
    for (const auto& tab : vm.horizontal_tabs)
      if (tab.active &&
          (tab.id != scrolled_tab_ || vm.changed.Has(Dirty::kTabs))) {
        const auto record = records_.find(tab.id);
        if (record != records_.end()) {
          tabs_.ScrollIntoView(record->second.item);
          scrolled_tab_ = tab.id;
        }
        break;
      }
  rendered_ = true;
}

void TabList::SetHorizontal(bool horizontal) {
  if (horizontal_ == horizontal)
    return;
  auto& previous_panel = panels_[horizontal_ ? 1 : 0];
  if (!previous_panel)
    previous_panel = tabs_.ItemsPanel();
  CancelDrag();
  // A new items panel discards the old containers without recycling them, so
  // the New tab row's button would stay in a row that is no longer shown.
  DetachNewTabButton();
  horizontal_ = horizontal;
  rendered_ = false;
  scrolled_tab_ = {};
  favorites_.ResetPointerState();
  tabs_.Header(horizontal ? nullptr : favorites_.Root().as<IInspectable>());
  auto& panel = panels_[horizontal ? 1 : 0];
  if (!panel)
    panel =
        xaml::Markup::XamlReader::Load(
            horizontal
                ? LR"(<ItemsPanelTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"><ItemsStackPanel Orientation="Horizontal" /></ItemsPanelTemplate>)"
                : LR"(<ItemsPanelTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"><ItemsStackPanel Orientation="Vertical" /></ItemsPanelTemplate>)")
            .as<controls::ItemsPanelTemplate>();
  tabs_.ItemsPanel(panel);
  // Keep the horizontal extent for virtualization and revealing the active
  // tab, while clipping overflow without a scrollbar or manual scrolling.
  ScrollViewer::SetHorizontalScrollBarVisibility(
      tabs_,
      horizontal ? ScrollBarVisibility::Hidden : ScrollBarVisibility::Disabled);
  ScrollViewer::SetVerticalScrollBarVisibility(
      tabs_,
      horizontal ? ScrollBarVisibility::Disabled : ScrollBarVisibility::Auto);
  ScrollViewer::SetHorizontalScrollMode(tabs_, ScrollMode::Disabled);
  ScrollViewer::SetVerticalScrollMode(
      tabs_, horizontal ? ScrollMode::Disabled : ScrollMode::Enabled);
  tab_scrolled_.revoke();
  tab_scroll_ = nullptr;
  tab_widths_ = {};
  held_widths_.reset();
  hover_width_ = -1;
  for (const auto& [key, row] : rows_) {
    row->hovered = false;
    row->preview->SetTarget({}, {}, {});
    row->container.Width(std::numeric_limits<double>::quiet_NaN());
    SizeHorizontalRow(*row);
  }
  UpdateClears();
}

void TabList::DetachNewTabButton() {
  if (!new_tab_button_)
    return;
  const auto parent = new_tab_button_.Parent().try_as<Panel>();
  std::uint32_t at = 0;
  if (parent && parent.Children().IndexOf(new_tab_button_, at))
    parent.Children().RemoveAt(at);
}

void TabList::SetHovered(TabRow& row, bool hovered) {
  row.hovered = hovered;
  if (!row.id && !row.folder)
    return;
  context_.motion.PrepareBackground(row.background,
                                    context_.brushes.high_contrast());
  row.background.Background(
      row.active
          ? context_.glass.For(row.background, GlassRole::kActiveTab,
                               context_.brushes.active, kTokens.radius.tab)
      : hovered ? context_.brushes.hover
                : context_.brushes.transparent);
  row.close.control.Opacity(row.active || hovered ? 1 : 0);
  row.close.control.IsHitTestVisible(row.content.close &&
                                     (row.active || hovered));
}

void TabList::ShowClear(TabRow& row, bool today_header) {
  row.today_header = today_header;
  if (today_header && !row.clear) {
    const auto& tokens = kTokens.clear_tabs;
    auto clear = std::make_unique<ClearTabsButton>();
    clear->root = Grid();
    AddColumn(clear->root, 1, xaml::GridUnitType::Star);
    AddColumn(clear->root, 0, xaml::GridUnitType::Auto);
    Grid::SetColumnSpan(clear->root, 5);
    clear->line = Border();
    clear->line.Height(kTokens.spacing.separator_thickness);
    clear->line.VerticalAlignment(xaml::VerticalAlignment::Center);
    clear->line.Background(context_.brushes.separator);
    clear->label = Label(clear_text_, tokens.font_size);
    clear->label.FontWeight(kMedium);
    clear->label.Foreground(context_.brushes.text);
    auto icon = SourceIcon(context_.brushes.text, design::icons::kDartDown);
    icon.Width(tokens.icon_size);
    icon.Height(tokens.icon_size);
    clear->icon = icon;
    clear->content = StackPanel();
    clear->content.Orientation(Orientation::Horizontal);
    clear->content.Spacing(tokens.gap);
    clear->content.VerticalAlignment(xaml::VerticalAlignment::Center);
    clear->content.Margin(
        Thickness{tokens.padding_start, 0, kTokens.spacing.separator_inset, 0});
    clear->content.Children().Append(clear->label);
    clear->content.Children().Append(clear->icon);
    clear->content.OpacityTransition(xaml::ScalarTransition());
    clear->control = Button();
    clear->control.Content(clear->content);
    Grid::SetColumn(clear->control, 1);
    clear->control.MinWidth(0);
    clear->control.MinHeight(0);
    clear->control.VerticalAlignment(xaml::VerticalAlignment::Stretch);
    clear->control.Padding(UniformThickness(0));
    clear->control.BorderThickness(UniformThickness(0));
    clear->control.CornerRadius(UniformCorners(0));
    clear->control.Background(context_.brushes.transparent);
    for (const auto* key :
         {L"ButtonBackgroundPointerOver", L"ButtonBackgroundPressed",
          L"ButtonBorderBrushPointerOver", L"ButtonBorderBrushPressed"})
      clear->control.Resources().Insert(winrt::box_value(winrt::hstring(key)),
                                        context_.brushes.transparent);
    Accessible(clear->control, clear_text_);
    xaml::Automation::AutomationProperties::SetHelpText(clear->control,
                                                        clear_help_text_);
    SetToolTip(clear->control, clear_help_text_);
    clear->click = clear->control.Click(
        winrt::auto_revoke,
        [this](auto&&, auto&&) { context_.delegate.OnClearTabs(); });
    const auto hover = [this, native = &row](bool hovered) {
      native->clear->hovered = hovered;
      UpdateClear(*native);
    };
    clear->entered = clear->control.PointerEntered(
        winrt::auto_revoke, [hover](auto&&, auto&&) { hover(true); });
    clear->exited = clear->control.PointerExited(
        winrt::auto_revoke, [this, native = &row, hover](auto&&, auto&&) {
          // Descendant exits also reach the Button while crossing its label
          // and icon. Only leaving the whole control ends its hover.
          if (!context_.pointer_over(native->clear->control))
            hover(false);
        });
    // Only keyboard focus reveals the label; a click leaves it to the pointer.
    const auto focus = [this, native = &row] {
      auto& button = *native->clear;
      button.has_focus =
          button.control.FocusState() == xaml::FocusState::Keyboard;
      UpdateClear(*native);
    };
    clear->focused = clear->control.GotFocus(
        winrt::auto_revoke, [focus](auto&&, auto&&) { focus(); });
    clear->blurred = clear->control.LostFocus(
        winrt::auto_revoke, [focus](auto&&, auto&&) { focus(); });
    clear->root.Children().Append(clear->line);
    clear->root.Children().Append(clear->control);
    row.grid.Children().Append(clear->root);
    row.clear = std::move(clear);
  }
  UpdateClear(row);
}

void TabList::UpdateClear(TabRow& row) {
  if (!row.clear)
    return;
  auto& clear = *row.clear;
  const bool shown = row.today_header && !icon_only_ && !horizontal_;
  clear.root.Visibility(shown ? Visibility::Visible : Visibility::Collapsed);
  if (!row.today_header)
    return;
  row.divider.Visibility(shown ? Visibility::Collapsed : Visibility::Visible);
  // Disabled items disable their content, so this header is enabled while
  // it carries the button.
  row.container.IsEnabled(shown);
  if (!shown)
    return;
  // Zen runs the separator under the label until the sidebar is hovered, then
  // ends it at the button and shows the label at half strength.
  const bool revealed = list_hovered_ || clear.has_focus;
  Grid::SetColumnSpan(clear.line, revealed ? 1 : 2);
  clear.line.Margin(Thickness{kTokens.spacing.separator_inset, 0,
                              revealed ? 0 : kTokens.spacing.separator_inset,
                              0});
  clear.control.IsHitTestVisible(revealed);
  clear.icon.Visibility(today_idle_ ? Visibility::Visible
                                    : Visibility::Collapsed);
  clear.content.OpacityTransition().Duration(
      Ms(context_.motion.Enabled(context_.brushes.high_contrast())
             ? kTokens.clear_tabs.fade_ms
             : 0));
  clear.content.Opacity(clear.hovered || clear.has_focus ? 1
                        : revealed ? kTokens.clear_tabs.sidebar_hover_opacity
                                   : 0);
}

void TabList::UpdateClears() {
  for (const auto& [key, row] : rows_)
    UpdateClear(*row);
}

void TabList::CloseWithPointer(const TabRow& row) {
  if (!row.id)
    return;
  if (horizontal_) {
    SetListHovered(true);
    held_widths_ = tab_widths_;
  }
  context_.delegate.OnTabClosed(row.id);
}

void TabList::SetHoverWidth(float width) {
  if (hover_width_ == width)
    return;
  hover_width_ = width;
  // Cards wait longer as tabs, or the sidebar, widen.
  const auto& h = kTokens.horizontal_tabs;
  context_.tab_previews.SetShowDelay(
      horizontal_
          ? TabPreviewShowDelay(width, h.tab_pinned_width, h.tab_standard_width)
          : TabPreviewShowDelay(width, kTokens.sidebar.collapsed_width,
                                kTokens.sidebar.default_width));
}

void TabList::SetListHovered(bool hovered) {
  if (list_hovered_ == hovered)
    return;
  list_hovered_ = hovered;
  if (!hovered && held_widths_) {
    held_widths_.reset();
    LayoutHorizontalRows();
  }
  UpdateClears();
}

void TabList::CancelPointerInteraction() {
  for (const auto& [key, row] : rows_)
    row->middle_target = {};
}

void TabList::ResetPointerState() {
  CancelDrag();
  favorites_.ResetPointerState();
  CancelPointerInteraction();
  for (const auto& [key, row] : rows_)
    if (row->clear)
      row->clear->hovered = false;
  SetListHovered(false);
  UpdateClears();
  DismissHover();
}

void TabList::RefreshPointerHover() {
  // Capture changes and routing between islands are not physical exits.
  // Reconcile once after the input event, when native hit testing has settled.
  const bool hovered = context_.pointer_over(tabs_);
  SetListHovered(hovered);
  if (!hovered)
    DismissHover();
}

void TabList::DismissHover() {
  favorites_.ClosePreviews();
  for (const auto& [key, row] : rows_) {
    // Stop pending hover delays too, so a scrolled-out or dismissed row cannot
    // open a card after the dismissal.
    row->preview->Close();
    if (row->hovered)
      SetHovered(*row, false);
  }
}

void TabList::SizeHorizontalRow(TabRow& row) {
  if (row.horizontal_hit_target != horizontal_) {
    row.horizontal_hit_target = horizontal_;
    // Keep the pill's inset inside the clickable content,
    // while letting a maximized tab's hit target reach the screen's top edge.
    row.container.Margin(
        {kTokens.spacing.tab_inset, horizontal_ ? 0 : kTokens.spacing.tab_gap,
         kTokens.spacing.tab_inset, horizontal_ ? 0 : kTokens.spacing.tab_gap});
    row.background.Margin(horizontal_ ? Thickness{0, kTokens.spacing.tab_gap, 0,
                                                  kTokens.spacing.tab_gap}
                                      : UniformThickness(0));
  }
  if (!row.id)
    return;
  const double height =
      horizontal_
          ? HorizontalTabHeight(static_cast<float>(tabs_.ActualHeight()))
          : kTokens.spacing.tab_height;
  if (row.background.Height() != height)
    row.background.Height(height);
  const double button_height =
      std::min<double>(kTokens.spacing.close_button_size, height);
  if (row.close.control.Height() != button_height)
    row.close.control.Height(button_height);
  if (row.audio.control.Height() != button_height)
    row.audio.control.Height(button_height);
  if (!horizontal_)
    return;
  const double width =
      row.data.active ? tab_widths_.active : tab_widths_.inactive;
  if (width > 0 && row.container.Width() != width)
    row.container.Width(width);
}

void TabList::LayoutHorizontalRows() {
  if (!horizontal_)
    return;
  tab_widths_ =
      HorizontalTabWidthsFor(static_cast<float>(tabs_.ActualWidth()),
                             records_.size(), static_cast<bool>(active_tab_));
  if (held_widths_) {
    // Tabs added meanwhile still make room; closed ones leave a gap.
    tab_widths_.active = std::min(tab_widths_.active, held_widths_->active);
    tab_widths_.inactive =
        std::min(tab_widths_.inactive, held_widths_->inactive);
  }
  SetHoverWidth(std::max(tab_widths_.active, tab_widths_.inactive));
  for (const auto& [key, row] : rows_)
    if (row->id) {
      SizeHorizontalRow(*row);
      ApplyRowContent(*row);
    }
  if (horizontal_extent_changed_)
    horizontal_extent_changed_(HorizontalTabsExtent(
        tab_widths_, records_.size(), records_.contains(active_tab_)));
}

void TabList::SetIconOnly(bool icons) {
  if (icon_only_ == icons)
    return;
  icon_only_ = icons;
  for (auto& [id, record] : folder_records_) {
    if (record.row) {
      record.row->configured = false;
      record.row->folder = {};
      UpdateFolderRow(record);
    }
  }
  for (auto&& [id, record] : records_)
    UpdateTabRow(record, false);
  for (const auto& [key, row] : rows_)
    if (!row->id && !row->folder)
      row->title.Visibility(Visibility::Collapsed);
  UpdateClears();
}

void TabList::SetText(UiTextView text) {
  picker_labels_ = {NativeText(UiText::kWorkspaceGlyph, text),
                    NativeText(UiText::kWorkspaceChooseIcon, text),
                    NativeText(UiText::kWorkspaceSearchIcons, text),
                    NativeText(UiText::kWorkspaceIcons, text),
                    NativeText(UiText::kWorkspaceEmoji, text),
                    NativeText(UiText::kWorkspaceCustomEmoji, text)};

  constexpr std::array folder_keys{
      UiText::kFolder,           UiText::kNewFolder,
      UiText::kNewSubfolder,     UiText::kRenameFolder,
      UiText::kUnpackFolder,     UiText::kDeleteFolder,
      UiText::kMoveFolderToRoot, UiText::kSaveFolder,
      UiText::kFolderIcon,       UiText::kMoveFolderWorkspace};
  for (std::size_t i = 0; i < folder_keys.size(); ++i)
    folder_text_[i] = NativeText(folder_keys[i], text);
  Accessible(tabs_, NativeText(UiText::kTabs, text));
  close_text_ = NativeText(UiText::kCloseTab, text);
  mute_text_ = NativeText(UiText::kMuteTab, text);
  unmute_text_ = NativeText(UiText::kUnmuteTab, text);
  clear_text_ = NativeText(UiText::kClearTabs, text);
  clear_help_text_ = NativeText(UiText::kClearTabsHelp, text);
  favorites_.SetText(text);
  for (const auto& [key, row] : rows_) {
    SetButtonText(row->close, close_text_);
    row->preview->SetText(text);
    if (const auto& clear = row->clear) {
      clear->label.Text(clear_text_);
      Accessible(clear->control, clear_text_);
      xaml::Automation::AutomationProperties::SetHelpText(clear->control,
                                                          clear_help_text_);
      SetToolTip(clear->control, clear_help_text_);
    }
  }
  for (const auto& [id, record] : records_)
    if (record.row) {
      record.row->audio_text =
          record.row->data.muted ? unmute_text_ : mute_text_;
      SetButtonText(record.row->audio, record.row->audio_text);
    }
}

bool TabList::IsPreviewOpen() const {
  if (favorites_.IsPreviewOpen())
    return true;
  return std::ranges::any_of(
      rows_, [](const auto& entry) { return entry.second->preview->IsOpen(); });
}

void TabList::AppendAnchors(std::vector<Anchor>& anchors,
                            const ShellLayout& layout) {
  if (!horizontal_)
    favorites_.AppendAnchors(anchors, layout.sidebar_visible);
  const Rect viewport = horizontal_ ? layout.tab_strip : layout.sidebar;
  const bool visible =
      horizontal_ ? layout.tab_strip_visible : layout.sidebar_visible;
  WatchScroll();
  for (const auto& [id, record] : records_) {
    if (!record.row || !record.row->background.Parent())
      continue;
    const auto bounds = horizontal_ ? context_.bounds(record.row->hit_target)
                                    : context_.bounds(record.row->background);
    if (bounds.bottom() >= viewport.y && bounds.y < viewport.bottom() &&
        bounds.right() > viewport.x && bounds.x < viewport.right())
      anchors.push_back({AnchorKind::kTab, id.value, bounds, visible});
  }
}

void TabList::BuildTabs() {
  tabs_.Header(favorites_.Root());
  items_ = winrt::single_threaded_observable_vector<IInspectable>();
  tabs_.ItemsSource(items_);
  tabs_.SelectionMode(ListViewSelectionMode::None);
  tabs_.IsItemClickEnabled(true);
  tabs_.Background(context_.brushes.transparent);
  tabs_.BorderThickness(UniformThickness(0));
  tabs_.Padding(UniformThickness(0));
  ScrollViewer::SetHorizontalScrollBarVisibility(tabs_,
                                                 ScrollBarVisibility::Disabled);
  ScrollViewer::SetVerticalScrollBarVisibility(tabs_,
                                               ScrollBarVisibility::Auto);
  Accessible(tabs_, NativeText(UiText::kTabs));
  sized_ = tabs_.SizeChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
    LayoutHorizontalRows();
    if (horizontal_) {
      const auto active = records_.find(active_tab_);
      if (active != records_.end())
        tabs_.ScrollIntoView(active->second.item);
    }
  });
  list_entered_ = tabs_.PointerEntered(
      winrt::auto_revoke, [this](auto&&, auto&&) { SetListHovered(true); });
  list_exited_ = tabs_.PointerExited(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        // Rows' exits bubble here while the pointer is still over the list.
        const auto at = args.GetCurrentPoint(tabs_).Position();
        if (at.X < 0 || at.Y < 0 || at.X >= tabs_.ActualWidth() ||
            at.Y >= tabs_.ActualHeight())
          SetListHovered(false);
      });
  tab_click_ = tabs_.ItemClick(
      winrt::auto_revoke, [this](auto&&, const ItemClickEventArgs& args) {
        if (const auto value =
                args.ClickedItem().try_as<foundation::IPropertyValue>();
            value && value.Type() == foundation::PropertyType::UInt64) {
          context_.delegate.OnTabActivated(TabId{value.GetUInt64()});
        } else if (value && value.Type() == foundation::PropertyType::Int64) {
          const FolderId id{std::bit_cast<std::uint64_t>(value.GetInt64())};
          if (const auto folder = context_.delegate.OnFolderRequested(id))
            (void)context_.delegate.OnFolderCommand(
                {.kind = FolderCommandKind::kCollapse,
                 .folder = id,
                 .collapsed = !folder->collapsed});
        }
      });
  // Rows move with a pointer drag the list captures (tab_list_drag.cpp).
  // The list still accepts system drops: URLs dragged from pages or other
  // applications land on the sidebar's URL drop target.
  tabs_.AllowDrop(true);
  InstallDrag();
  // WinUI owns ListViewItem.Content. A DataTemplate owns the row visuals;
  // ContainerContentChanging only updates that template's existing controls.
  // Setting Content here would race ItemsSource's realization/recycling.
  tabs_.ItemTemplate(xaml::Markup::XamlReader::Load(LR"(
      <DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
        <Grid x:Name="RowHitTarget">
        <Border x:Name="RowBackground">
          <Grid>
            <Border x:Name="RowDivider" Grid.ColumnSpan="5" />
            <TextBlock x:Name="RowGlyph" />
            <Image x:Name="RowImage" />
            <Button x:Name="RowAudio" Grid.Column="1"><TextBlock /></Button>
            <TextBlock x:Name="RowTitle" Grid.Column="2" />
            <TextBlock x:Name="RowStatus" Grid.Column="3" />
            <Button x:Name="RowClose" Grid.Column="4"><TextBlock /></Button>
          </Grid>
        </Border>
        </Grid>
      </DataTemplate>)")
                         .as<xaml::DataTemplate>());
  realize_tab_ = [this, weak = std::weak_ptr<int>(lifetime_)](
                     const ListViewBase&,
                     const ContainerContentChangingEventArgs& args) {
    if (weak.expired())
      return;
    try {
      auto container = args.ItemContainer().as<ListViewItem>();
      const auto key = winrt::get_abi(container);
      drag_slots_dirty_ = true;
      if (args.InRecycleQueue()) {
        if (const auto found = rows_.find(key); found != rows_.end()) {
          auto& row = *found->second;
          if (drag_.row == &row) {
            drag_.row = nullptr;
            if (drag_visuals_)
              drag_visuals_->DetachRow();
          }
          if (new_tab_button_ && new_tab_button_.Parent() == row.grid) {
            std::uint32_t at = 0;
            if (row.grid.Children().IndexOf(new_tab_button_, at))
              row.grid.Children().RemoveAt(at);
          }
          row.editing_folder = {};
          if (row.rename_picker)
            row.rename_picker->Close();
          if (row.rename_flyout)
            row.rename_flyout.Hide();
          row.middle_target = {};
          row.hovered = false;
          row.today_header = false;
          if (row.clear) {
            row.clear->hovered = false;
            row.clear->has_focus = false;
          }
          row.preview->SetTarget({}, {}, {});
          row.background.BackgroundTransition(nullptr);
          if (const auto item = records_.find(row.id);
              item != records_.end() && item->second.row == &row)
            item->second.row = nullptr;
          if (const auto folder = folder_records_.find(row.folder);
              folder != folder_records_.end() && folder->second.row == &row)
            folder->second.row = nullptr;
          row.folder = {};
          row.id = {};
          if (row.folder_icon) {
            row.folder_icon->Reset();
            row.folder_icon->Root().Visibility(Visibility::Collapsed);
          }
        }
        return;
      }
      const auto content = container.ContentTemplateRoot().try_as<Grid>();
      const auto background =
          content ? content.FindName(L"RowBackground").try_as<Border>()
                  : Border{nullptr};
      if (!background) {
        if (args.Phase() < 2)
          args.RegisterUpdateCallback(args.Phase() + 1, realize_tab_);
        return;
      }
      auto& cached = rows_[key];
      if (!cached)
        cached = CreateTabRow(container, background, content);
      auto& row = *cached;
      if (new_tab_button_ && new_tab_button_.Parent() == row.grid) {
        std::uint32_t at = 0;
        if (row.grid.Children().IndexOf(new_tab_button_, at))
          row.grid.Children().RemoveAt(at);
      }
      if (const auto previous = records_.find(row.id);
          previous != records_.end() && previous->second.row == &row)
        previous->second.row = nullptr;
      row.preview->SetTarget({}, {}, {});
      row.background.BackgroundTransition(nullptr);
      if (const auto folder = folder_records_.find(row.folder);
          folder != folder_records_.end() && folder->second.row == &row)
        folder->second.row = nullptr;
      const auto value = args.Item().as<foundation::IPropertyValue>();
      const bool same_folder =
          row.folder && value.Type() == foundation::PropertyType::Int64 &&
          row.folder.value == std::bit_cast<std::uint64_t>(value.GetInt64());
      if (!same_folder) {
        if (row.folder)
          row.configured = false;
        row.folder = {};
        if (row.folder_icon) {
          row.folder_icon->Reset();
          row.folder_icon->Root().Visibility(Visibility::Collapsed);
        }
      }
      row.id = {};
      row.middle_target = {};
      if (value.Type() == foundation::PropertyType::Int64) {
        const FolderId id{std::bit_cast<std::uint64_t>(value.GetInt64())};
        const auto folder = folder_records_.find(id);
        if (folder != folder_records_.end()) {
          folder->second.row = &row;
          UpdateFolderRow(folder->second);
        }
        args.Handled(true);
        return;
      }
      if (value.Type() == foundation::PropertyType::String) {
        row.background.Background(context_.brushes.transparent);
        row.title.Text(L"");
        row.title.FontSize(kTokens.type.size_small);
        row.title.Foreground(context_.brushes.muted);
        row.glyph.Visibility(Visibility::Collapsed);
        row.image.Visibility(Visibility::Collapsed);
        row.status.Visibility(Visibility::Collapsed);
        row.audio.control.Visibility(Visibility::Collapsed);
        row.close.control.Visibility(Visibility::Collapsed);
        row.title.Visibility(Visibility::Collapsed);
        row.background.Padding(UniformThickness(0));
        row.divider.Visibility(Visibility::Visible);
        row.background.Height(kTokens.spacing.separator_height);
        container.IsEnabled(false);
        container.IsTabStop(false);
        ShowClear(row, SameItem(args.Item(), today_header_));
        if (SameItem(args.Item(), new_tab_item_) && new_tab_button_) {
          ShowClear(row, false);
          row.divider.Visibility(Visibility::Collapsed);
          row.background.Height(kTokens.spacing.tab_height);
          row.background.Padding(UniformThickness(0));
          container.IsEnabled(true);
          // The nested native button is the one keyboard stop for this row.
          container.IsTabStop(false);
          DetachNewTabButton();
          Grid::SetColumnSpan(new_tab_button_, 5);
          row.grid.Children().Append(new_tab_button_);
        }
      } else {
        const auto found = records_.find(TabId{value.GetUInt64()});
        if (found == records_.end())
          return;
        auto& record = found->second;
        const auto tab = context_.delegate.OnTabRequested(record.id);
        if (!tab)
          return;
        row.id = record.id;
        record.row = &row;
        container.IsEnabled(true);
        container.IsTabStop(true);
        ShowClear(row, false);
        (void)UpdateTabRecord(record, *tab);
        UpdateTabRow(record, false);
      }
      args.Handled(true);
      anchors_dirty_ = true;
    } catch (const winrt::hresult_error& error) {
      OutputDebugStringW(error.message().c_str());
      platform::WriteDefaultLog(LogLevel::kError,
                                "WinUI tab realization failed");
    } catch (...) {
      platform::WriteDefaultLog(LogLevel::kError, "Tab realization failed");
    }
  };
  containers_ =
      tabs_.ContainerContentChanging(winrt::auto_revoke, realize_tab_);
}

std::unique_ptr<TabRow> TabList::CreateTabRow(const ListViewItem& container,
                                              const Border& background,
                                              const Grid& hit_target) {
  auto result = std::make_unique<TabRow>();
  auto& row = *result;
  row.container = container;
  row.background = background;
  row.hit_target = hit_target;
  row.hit_target.Background(context_.brushes.transparent);
  row.grid = background.Child().as<Grid>();
  row.divider = background.FindName(L"RowDivider").as<Border>();
  row.divider.Height(kTokens.spacing.separator_thickness);
  row.divider.Margin(Thickness{kTokens.spacing.separator_inset, 0,
                               kTokens.spacing.separator_inset, 0});
  row.divider.VerticalAlignment(xaml::VerticalAlignment::Center);
  row.divider.Background(context_.brushes.separator);
  row.glyph = background.FindName(L"RowGlyph").as<TextBlock>();
  row.image = background.FindName(L"RowImage").as<controls::Image>();
  row.title = background.FindName(L"RowTitle").as<TextBlock>();
  row.status = background.FindName(L"RowStatus").as<TextBlock>();
  row.close.control = background.FindName(L"RowClose").as<Button>();
  row.close.glyph = row.close.control.Content().as<TextBlock>();
  row.audio.control = background.FindName(L"RowAudio").as<Button>();
  row.audio.glyph = row.audio.control.Content().as<TextBlock>();
  container.Padding(UniformThickness(0));
  container.Margin(Thickness{kTokens.spacing.tab_inset, kTokens.spacing.tab_gap,
                             kTokens.spacing.tab_inset,
                             kTokens.spacing.tab_gap});
  // The native item minimum is wider than the collapsed rail's content area.
  // Let the virtualizing panel constrain the row to the available width.
  container.MinWidth(0);
  container.MinHeight(0);
  container.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
  row.background.CornerRadius(UniformCorners(kTokens.radius.tab));
  row.background.Height(kTokens.spacing.tab_height);
  row.background.Padding(Thickness{kTokens.spacing.tab_padding, 0, 0, 0});
  AddColumn(row.grid, kTokens.spacing.icon_size + kTokens.spacing.tab_icon_gap,
            xaml::GridUnitType::Pixel);
  AddColumn(row.grid, 0, xaml::GridUnitType::Auto);
  AddColumn(row.grid, 1, xaml::GridUnitType::Star);
  AddColumn(row.grid, 0, xaml::GridUnitType::Auto);
  AddColumn(row.grid, kTokens.spacing.close_button_size,
            xaml::GridUnitType::Pixel);
  for (auto label : {row.glyph, row.status, row.close.glyph, row.audio.glyph}) {
    label.FontFamily(
        media::FontFamily(winrt::to_hstring(kTokens.type.icon_family)));
    label.FontSize(kTokens.spacing.icon_size);
    label.VerticalAlignment(xaml::VerticalAlignment::Center);
    label.Foreground(context_.brushes.muted);
  }
  row.glyph.Text(L"\uE774");
  row.close.glyph.Text(winrt::hstring(glyphs::kClose));
  // Close and audio icons follow the chrome's icon pack.
  row.close.icon.reset(new IconView(context_.icons, IconRole::kClose,
                                    kTokens.icon_packs.tab_close_size,
                                    context_.brushes.muted));
  row.close.control.Content(row.close.icon->Root());
  row.audio.icon.reset(new IconView(context_.icons, IconRole::kSpeaker,
                                    kTokens.spacing.icon_size,
                                    context_.brushes.muted));
  row.audio.control.Content(row.audio.icon->Root());
  row.image.Width(kTokens.spacing.icon_size);
  row.image.Height(kTokens.spacing.icon_size);
  row.image.HorizontalAlignment(xaml::HorizontalAlignment::Left);
  row.title.FontFamily(
      media::FontFamily(winrt::to_hstring(kTokens.type.family)));
  row.title.VerticalAlignment(xaml::VerticalAlignment::Center);
  row.title.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
  row.close.control.Width(kTokens.spacing.close_button_size);
  row.close.control.Height(kTokens.spacing.close_button_size);
  row.close.glyph.FontSize(kTokens.spacing.close_glyph_size);
  row.close.control.MinWidth(0);
  row.close.control.MinHeight(0);
  row.close.control.Padding(UniformThickness(0));
  row.close.control.BorderThickness(UniformThickness(0));
  row.close.control.Background(context_.brushes.transparent);
  row.close.control.CornerRadius(UniformCorners(kTokens.radius.button));
  SetButtonText(row.close, close_text_);
  row.close.click = row.close.control.Click(
      winrt::auto_revoke,
      [this, native = &row](auto&&, auto&&) { CloseWithPointer(*native); });
  row.audio.control.Width(kTokens.spacing.icon_size +
                          kTokens.spacing.tab_icon_gap);
  row.audio.control.Height(kTokens.spacing.close_button_size);
  row.audio.control.MinWidth(0);
  row.audio.control.MinHeight(0);
  row.audio.glyph.IsTextScaleFactorEnabled(false);
  row.audio.control.Padding(UniformThickness(0));
  row.audio.control.BorderThickness(UniformThickness(0));
  row.audio.control.Background(context_.brushes.transparent);
  row.audio.control.CornerRadius(UniformCorners(kTokens.radius.button));
  row.audio.click = row.audio.control.Click(
      winrt::auto_revoke, [this, native = &row](auto&&, auto&&) {
        if (native->id)
          context_.delegate.OnTabMuteToggled(native->id);
      });
  row.audio_tapped = row.audio.control.Tapped(
      winrt::auto_revoke,
      [](auto&&, const xaml::Input::TappedRoutedEventArgs& args) {
        args.Handled(true);
      });
  row.preview =
      std::make_unique<TabHoverCard>(context_, row.background, container);
  row.preview->Configure(preview_metrics_);
  row.context = row.hit_target.RightTapped(
      winrt::auto_revoke,
      [this, native = &row](
          auto&&, const xaml::Input::RightTappedRoutedEventArgs& args) {
        if (native->folder) {
          ShowFolderMenu(*native);
          args.Handled(true);
        } else if (native->id) {
          context_.request_menu(MenuKind::kTab, native->id.value,
                                context_.bounds(native->background));
          args.Handled(true);
        }
      });
  // A left mouse press activates the tab, before any drag; the click that
  // follows changes nothing. Touch and keys activate through ItemClick.
  row.pressed = row.hit_target.PointerPressed(
      winrt::auto_revoke,
      [this, native = &row](auto&&,
                            const xaml::Input::PointerRoutedEventArgs& args) {
        if (!native->id ||
            args.Pointer().PointerDeviceType() !=
                winrt::Microsoft::UI::Input::PointerDeviceType::Mouse)
          return;
        const auto buttons =
            args.GetCurrentPoint(native->background).Properties();
        if (buttons.PointerUpdateKind() ==
            winrt::Microsoft::UI::Input::PointerUpdateKind::
                MiddleButtonPressed) {
          native->middle_target = native->id;
          native->middle_pointer = args.Pointer().PointerId();
        }
        if (!native->data.active && buttons.IsLeftButtonPressed() &&
            !buttons.IsRightButtonPressed() && !buttons.IsMiddleButtonPressed())
          context_.delegate.OnTabActivated(native->id);
      });
  // A middle click released over the tab closes it.
  row.released = row.hit_target.PointerReleased(
      winrt::auto_revoke,
      [this, native = &row](auto&&,
                            const xaml::Input::PointerRoutedEventArgs& args) {
        if (args.GetCurrentPoint(native->background)
                .Properties()
                .PointerUpdateKind() !=
            winrt::Microsoft::UI::Input::PointerUpdateKind::
                MiddleButtonReleased)
          return;
        const auto target = std::exchange(native->middle_target, TabId{});
        if (!target || target != native->id ||
            native->middle_pointer != args.Pointer().PointerId() ||
            !context_.pointer_over(native->hit_target))
          return;
        CloseWithPointer(*native);
        args.Handled(true);
      });
  row.capture_lost = row.hit_target.PointerCaptureLost(
      winrt::auto_revoke,
      [native = &row](auto&&, auto&&) { native->middle_target = {}; });
  row.canceled = row.hit_target.PointerCanceled(
      winrt::auto_revoke,
      [native = &row](auto&&, auto&&) { native->middle_target = {}; });
  row.entered = row.hit_target.PointerEntered(
      winrt::auto_revoke,
      [this, native = &row](auto&&, auto&&) { SetHovered(*native, true); });
  row.exited = row.hit_target.PointerExited(
      winrt::auto_revoke, [this, native = &row](auto&&, auto&&) {
        if (!context_.pointer_over(native->hit_target)) {
          native->middle_target = {};
          SetHovered(*native, false);
        }
      });
  return result;
}

void TabList::UpdateTabRow(TabRecord& record, bool animate_background) {
  if (!record.row)
    return;
  auto& row = *record.row;
  if (row.folder)
    row.configured = false;
  row.folder = {};
  if (row.folder_icon)
    row.folder_icon->Root().Visibility(Visibility::Collapsed);
  row.depth = record.depth;
  row.background.Padding(
      Thickness{kTokens.spacing.tab_padding * static_cast<float>(row.depth + 1),
                0, 0, 0});
  auto& data = row.data;
  const auto border = data.active && data.essential
                          ? context_.brushes.favorite_active_border
                          : context_.brushes.transparent;
  if (row.background.BorderBrush() != border)
    row.background.BorderBrush(border);
  const auto border_width = UniformThickness(
      data.essential ? kTokens.spacing.favorite_active_border_width : 0);
  if (row.background.BorderThickness() != border_width)
    row.background.BorderThickness(border_width);
  if (data.title_dirty) {
    data.native_title = winrt::to_hstring(data.title);
    data.title_dirty = false;
  }
  row.divider.Visibility(Visibility::Collapsed);
  row.active = data.active;
  row.title.FontSize(kTokens.type.size);
  row.title.Foreground(context_.brushes.text);
  if (row.title.Text() != data.native_title) {
    row.title.Text(data.native_title);
    Accessible(row.background, data.native_title);
  }
  const auto background = data.active   ? context_.brushes.active
                          : row.hovered ? context_.brushes.hover
                                        : context_.brushes.transparent;
  if (row.background.Background() != background) {
    if (animate_background)
      context_.motion.PrepareBackground(row.background,
                                        context_.brushes.high_contrast());
    else
      row.background.BackgroundTransition(nullptr);
    row.background.Background(background);
  }
  row.preview->SetTarget(record.id, data.title, data.url);
  const auto bitmap = Bitmap(data.favicon);
  if (row.image.Source() != bitmap)
    row.image.Source(bitmap);
  row.audio.icon->SetRole(data.muted ? IconRole::kSpeakerOff
                                     : IconRole::kSpeaker);
  const auto& audio_text = record.row->data.muted ? unmute_text_ : mute_text_;
  if (row.audio_text != audio_text) {
    row.audio_text = audio_text;
    SetButtonText(row.audio, audio_text);
  }
  const std::wstring_view status = data.crashed     ? L"\uE7BA"
                                   : data.attention ? L"\uE734"
                                                    : L"";
  if (row.status.Text() != status)
    row.status.Text(winrt::hstring(status));
  row.close.control.Opacity(data.active || row.hovered ? 1 : 0);
  SizeHorizontalRow(row);
  ApplyRowContent(row);
  UpdateSplitRow(row);
}

void TabList::ApplyRowContent(TabRow& row) {
  const auto& data = row.data;
  const bool audio = data.muted || data.audible;
  const float width = data.active ? tab_widths_.active : tab_widths_.inactive;
  const auto content = horizontal_ && width > 0
                           ? HorizontalTabContent(width, data.active, audio)
                           : TabRowContent{.icon = true,
                                           .audio = audio,
                                           .close = !icon_only_ && !row.folder,
                                           .centered = icon_only_};
  if (!row.configured || row.content != content) {
    row.content = content;
    row.configured = true;
    using xaml::GridLength;
    using xaml::GridUnitType;
    const bool centered = content.centered;
    const auto columns = row.grid.ColumnDefinitions();
    columns.GetAt(0).Width(
        centered ? GridLength{1, GridUnitType::Star}
                 : GridLength{content.icon
                                  ? (row.folder ? kTokens.folder.size
                                                : kTokens.spacing.icon_size) +
                                        kTokens.spacing.tab_icon_gap
                                  : 0,
                              GridUnitType::Pixel});
    columns.GetAt(1).Width({0, GridUnitType::Auto});
    columns.GetAt(2).Width(centered ? GridLength{0, GridUnitType::Pixel}
                                    : GridLength{1, GridUnitType::Star});
    columns.GetAt(3).Width(
        {0, centered ? GridUnitType::Pixel : GridUnitType::Auto});
    columns.GetAt(4).Width(
        {content.close ? kTokens.spacing.close_button_size : 0,
         GridUnitType::Pixel});
    const auto alignment = centered ? xaml::HorizontalAlignment::Center
                                    : xaml::HorizontalAlignment::Left;
    row.glyph.HorizontalAlignment(alignment);
    row.image.HorizontalAlignment(alignment);
    row.audio.control.HorizontalAlignment(alignment);
    row.audio.control.HorizontalContentAlignment(alignment);
    row.audio.control.Width(centered ? kTokens.spacing.icon_size
                                     : kTokens.spacing.icon_size +
                                           kTokens.spacing.tab_icon_gap);
  }
  row.background.Padding(Thickness{
      content.centered
          ? 0
          : kTokens.spacing.tab_padding * static_cast<float>(row.depth + 1),
      0, 0, 0});
  row.title.Visibility(content.centered ? Visibility::Collapsed
                                        : Visibility::Visible);
  row.status.Visibility(content.centered ? Visibility::Collapsed
                                         : Visibility::Visible);
  row.close.control.Visibility(content.close ? Visibility::Visible
                                             : Visibility::Collapsed);
  const bool bitmap = static_cast<bool>(row.image.Source());
  row.image.Visibility(content.icon && bitmap ? Visibility::Visible
                                              : Visibility::Collapsed);
  row.glyph.Visibility(content.icon && !bitmap ? Visibility::Visible
                                               : Visibility::Collapsed);
  row.audio.control.Visibility(content.audio ? Visibility::Visible
                                             : Visibility::Collapsed);
  row.close.control.IsHitTestVisible(content.close &&
                                     (data.active || row.hovered));
  row.close.control.IsTabStop(data.active && content.close);
}

void TabList::UpdateActiveTab(const ShellViewModel& vm) {
  const TabId active =
      records_.contains(vm.active_tab_row) ? vm.active_tab_row : TabId{};
  if (active == active_tab_ && !vm.changed.Has(Dirty::kMultiPage))
    return;
  const auto previous = std::exchange(active_tab_, active);
  // The active tab's minimum width only applies while one is shown.
  if (horizontal_ && static_cast<bool>(previous) != static_cast<bool>(active))
    LayoutHorizontalRows();
  for (const auto id : {previous, active})
    if (const auto item = records_.find(id); item != records_.end()) {
      if (item->second.row) {
        if (const auto data = context_.delegate.OnTabRequested(id))
          (void)UpdateTabRecord(item->second, *data);
        item->second.row->data.active = id == active;
        UpdateTabRow(item->second);
      }
    }
}

bool TabList::UpdateTabRecord(TabRecord& record, const TabItemVm& tab) {
  auto& data = record.row->data;
  const bool changed =
      data.title != tab.title || data.url != tab.url ||
      data.favicon != tab.favicon || data.active != tab.active ||
      data.audible != tab.audible || data.muted != tab.muted ||
      data.crashed != tab.crashed || data.attention != tab.attention ||
      data.essential != tab.essential;
  if (changed) {
    if (data.title != tab.title) {
      data.title = tab.title;
      data.title_dirty = true;
    }
    if (data.url != tab.url)
      data.url = tab.url;
    data.favicon = tab.favicon;
    data.active = tab.active;
    data.audible = tab.audible;
    data.muted = tab.muted;
    data.crashed = tab.crashed;
    data.attention = tab.attention;
    data.essential = tab.essential;
  }
  return changed;
}

void TabList::ReconcileTabs(const ShellViewModel& vm) {
  const bool profile = !rendered_ && EnvFlag("KR_UI_PROFILE");
  const auto started = profile ? std::chrono::steady_clock::now()
                               : std::chrono::steady_clock::time_point{};
  active_tab_ = {};
  desired_.clear();
  ++generation_;
  images_dirty_ = vm.changed.Has(Dirty::kImages);
  const auto append_tab = [&](const TabItemVm& tab, std::size_t depth) {
    if (tab.active)
      active_tab_ = tab.id;
    const auto [entry, inserted] = records_.try_emplace(tab.id);
    auto& record = entry->second;
    if (inserted) {
      record.id = tab.id;
      record.item = winrt::box_value(tab.id.value);
    }
    const bool moved = record.depth != depth;
    record.depth = depth;
    record.seen = generation_;
    if (record.row) {
      if (UpdateTabRecord(record, tab) || moved || images_dirty_ ||
          !rendered_ || vm.changed.Has(Dirty::kMultiPage))
        UpdateTabRow(record);
    }
    desired_.push_back(record.item);
  };
  const auto append = [&](std::span<const TabItemVm> section,
                          IInspectable header) {
    if (section.empty())
      return;
    if (header)
      desired_.push_back(header);
    for (const auto& tab : section)
      append_tab(tab, 0);
  };
  if (horizontal_) {
    append(vm.horizontal_tabs, nullptr);
  } else {
    for (const auto& value : vm.folder_rows) {
      if (value.tab) {
        if (const auto tab = context_.delegate.OnTabRequested(value.tab))
          append_tab(*tab, value.depth);
      } else {
        const auto [entry, inserted] =
            folder_records_.try_emplace(value.folder);
        auto& record = entry->second;
        if (inserted) {
          record.id = value.folder;
          record.item =
              winrt::box_value(std::bit_cast<std::int64_t>(value.folder.value));
        }
        record.seen = generation_;
        record.depth = value.depth;
        record.contains_active = value.contains_active;
        UpdateFolderRow(record);
        desired_.push_back(record.item);
      }
    }
    append(vm.pinned, pinned_header_);
    desired_.push_back(today_header_);
    desired_.push_back(new_tab_item_);
    append(vm.today, nullptr);
  }
  const bool order_changed = displayed_.size() != desired_.size() ||
                             !std::equal(displayed_.begin(), displayed_.end(),
                                         desired_.begin(), SameItem);
  if (displayed_.empty() && !desired_.empty()) {
    // Publish the initial collection in one notification. Later changes
    // retain keyed inserts, moves and removals.
    items_.ReplaceAll(desired_);
    displayed_.assign(desired_.begin(), desired_.end());
  } else if (order_changed) {
    // Remove obsolete keys before reconciling positions. Leaving a closed
    // tab in the sequence would move every following tab around that key.
    const auto retained = [this, &vm](const IInspectable& item) {
      if (item == favorites_header_)
        return !horizontal_ && !vm.favorites.empty();
      if (item == pinned_header_)
        return !horizontal_ && !vm.pinned.empty();
      if (item == today_header_ || item == new_tab_item_)
        return !horizontal_;
      const auto value = item.as<foundation::IPropertyValue>();
      if (value.Type() == foundation::PropertyType::Int64) {
        const auto folder = folder_records_.find(
            FolderId{std::bit_cast<std::uint64_t>(value.GetInt64())});
        return folder != folder_records_.end() &&
               folder->second.seen == generation_;
      }
      const auto found = records_.find(TabId{value.GetUInt64()});
      return found != records_.end() && found->second.seen == generation_;
    };
    for (std::size_t index = displayed_.size(); index-- > 0;) {
      if (!retained(displayed_[index])) {
        items_.RemoveAt(static_cast<std::uint32_t>(index));
        displayed_.erase(displayed_.begin() + index);
      }
    }

    for (std::size_t index = 0; index < desired_.size(); ++index) {
      if (index < displayed_.size() &&
          SameItem(displayed_[index], desired_[index]))
        continue;
      auto existing =
          std::find(displayed_.begin() + std::min(index, displayed_.size()),
                    displayed_.end(), desired_[index]);
      if (existing == displayed_.end()) {
        items_.InsertAt(static_cast<std::uint32_t>(index), desired_[index]);
        displayed_.insert(displayed_.begin() + index, desired_[index]);
        continue;
      }

      // A forward move shifts a contiguous range left by one. Move its
      // original first key once instead of moving every intervening key.
      const auto target = std::find(desired_.begin() + index + 1,
                                    desired_.end(), displayed_[index]);
      const auto target_index =
          static_cast<std::size_t>(target - desired_.begin());
      if (existing == displayed_.begin() + index + 1 &&
          target_index < displayed_.size() &&
          std::equal(displayed_.begin() + index + 1,
                     displayed_.begin() + target_index + 1,
                     desired_.begin() + index, SameItem)) {
        const auto moved = displayed_[index];
        items_.RemoveAt(static_cast<std::uint32_t>(index));
        items_.InsertAt(static_cast<std::uint32_t>(target_index), moved);
        std::rotate(displayed_.begin() + index, displayed_.begin() + index + 1,
                    displayed_.begin() + target_index + 1);
      } else {
        const auto existing_index =
            static_cast<std::uint32_t>(existing - displayed_.begin());
        const auto moved = *existing;
        items_.RemoveAt(existing_index);
        items_.InsertAt(static_cast<std::uint32_t>(index), moved);
        std::rotate(displayed_.begin() + index, existing, existing + 1);
      }
    }
  }
  std::erase_if(records_, [this](const auto& entry) {
    if (entry.second.seen != generation_ && entry.second.row)
      entry.second.row->preview->SetTarget({}, {}, {});
    return entry.second.seen != generation_;
  });
  std::erase_if(folder_records_, [this](const auto& entry) {
    return entry.second.seen != generation_;
  });
  anchors_dirty_ = true;
  if (profile) {
    const auto elapsed = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    platform::WriteDefaultLog(
        LogLevel::kInfo,
        std::format("first tab reconcile: {:.3f} ms; native image/title "
                    "preparation limited to realized rows",
                    elapsed));
  }
}

bool TabList::SameItem(const IInspectable& a, const IInspectable& b) {
  if (a == b)
    return true;
  const auto av = a.as<foundation::IPropertyValue>();
  const auto bv = b.as<foundation::IPropertyValue>();
  return av.Type() == foundation::PropertyType::String &&
         bv.Type() == foundation::PropertyType::String &&
         av.GetString() == bv.GetString();
}

ScrollViewer TabList::FindScrollViewer(const xaml::DependencyObject& parent) {
  if (auto scroll = parent.try_as<ScrollViewer>())
    return scroll;
  const auto count = media::VisualTreeHelper::GetChildrenCount(parent);
  for (int index = 0; index < count; ++index) {
    if (auto scroll =
            FindScrollViewer(media::VisualTreeHelper::GetChild(parent, index)))
      return scroll;
  }
  return nullptr;
}

}  // namespace kr::win
