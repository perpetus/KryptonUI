#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "favorites_grid.hpp"
#include "folder_icon.hpp"
#include "krypton/ui/layout/horizontal_layout.hpp"
#include "krypton/ui/model/tab_drop.hpp"
#include "tab_drag_visuals.hpp"
#include "tab_hover_card.hpp"
#include "view_context.hpp"
#include "workspace_icon_picker.hpp"

namespace kr::win {
struct TabRowData {
  std::string title;
  std::string url;
  winrt::hstring native_title;
  bool title_dirty = true;
  ImageId favicon;
  bool active = false;
  bool audible = false;
  bool muted = false;
  bool crashed = false;
  bool attention = false;
  bool essential = false;
};

// Zen's Clear button at the end of the separator above unpinned tabs. Its
// row draws the separator itself, under the button until the list is hovered.
struct ClearTabsButton {
  controls::Grid root{nullptr};
  controls::Border line{nullptr};
  controls::Button control{nullptr};
  controls::StackPanel content{nullptr};
  controls::TextBlock label{nullptr};
  xaml::FrameworkElement icon{nullptr};
  controls::Button::Click_revoker click;
  xaml::UIElement::PointerEntered_revoker entered;
  xaml::UIElement::PointerExited_revoker exited;
  xaml::UIElement::GotFocus_revoker focused;
  xaml::UIElement::LostFocus_revoker blurred;
  bool hovered = false;
  bool has_focus = false;
};

struct SplitRowChrome {
  controls::Grid root{nullptr};
  std::array<controls::Button, kMaxSplitMembers> buttons{nullptr, nullptr,
                                                         nullptr, nullptr};
  std::array<controls::Border, kMaxSplitMembers> indicators{nullptr, nullptr,
                                                            nullptr, nullptr};
  std::array<controls::Image, kMaxSplitMembers> images{nullptr, nullptr,
                                                       nullptr, nullptr};
  std::array<controls::TextBlock, kMaxSplitMembers> glyphs{nullptr, nullptr,
                                                           nullptr, nullptr};
  std::array<TabId, kMaxSplitMembers> tabs{};
  std::array<std::string, kMaxSplitMembers> titles{};
  std::array<controls::Button::Click_revoker, kMaxSplitMembers> clicks;
  std::size_t count = 0;
};

struct TabRow {
  TabId id;
  FolderId folder;
  FolderId editing_folder;
  bool folder_collapsed = false;
  std::size_t depth = 0;
  controls::MenuFlyout folder_menu{nullptr};
  std::vector<controls::MenuFlyoutItem::Click_revoker> folder_clicks;
  controls::Flyout rename_flyout{nullptr};
  controls::TextBox rename_input{nullptr};
  std::unique_ptr<WorkspaceIconPicker> rename_picker;
  controls::Button rename_save{nullptr};
  controls::Button::Click_revoker rename_click;
  TabRowData data;
  controls::ListViewItem container{nullptr};
  bool horizontal_hit_target = false;
  controls::Grid hit_target{nullptr};
  controls::Border background{nullptr};
  controls::Border divider{nullptr};
  controls::Grid grid{nullptr};
  controls::TextBlock title{nullptr};
  controls::TextBlock glyph{nullptr};
  controls::TextBlock status{nullptr};
  controls::Image image{nullptr};
  std::unique_ptr<TabHoverCard> preview;
  std::unique_ptr<SplitRowChrome> split;
  std::unique_ptr<FolderIcon> folder_icon;
  // The content the row's columns are configured for.
  TabRowContent content;
  bool configured = false;
  bool hovered = false;
  bool active = false;
  xaml::UIElement::PointerEntered_revoker entered;
  xaml::UIElement::PointerExited_revoker exited;
  NativeButton close;
  NativeButton audio;
  winrt::hstring audio_text;
  xaml::UIElement::Tapped_revoker audio_tapped;
  xaml::UIElement::RightTapped_revoker context;
  xaml::UIElement::PointerPressed_revoker pressed;
  xaml::UIElement::PointerReleased_revoker released;
  xaml::UIElement::PointerCaptureLost_revoker capture_lost;
  xaml::UIElement::PointerCanceled_revoker canceled;
  TabId middle_target;
  std::uint32_t middle_pointer = 0;
  // Created when the row first shows the separator above unpinned tabs.
  std::unique_ptr<ClearTabsButton> clear;
  bool today_header = false;
};

struct TabRecord {
  TabId id;
  std::size_t depth = 0;
  std::uint64_t seen = 0;
  TabRow* row = nullptr;
  IInspectable item{nullptr};
};

struct FolderRecord {
  FolderId id;
  bool contains_active = false;
  std::size_t depth = 0;
  std::uint64_t seen = 0;
  TabRow* row = nullptr;
  IInspectable item{nullptr};
};

class TabList {
 public:
  TabList(ViewContext& context, bool& anchors_dirty);
  ~TabList();
  void Render(const ShellViewModel& vm);
  void SetIconOnly(bool icons);
  void SetHorizontal(bool horizontal);
  void SetHorizontalExtentChanged(
      std::move_only_function<void(float)> changed) {
    horizontal_extent_changed_ = std::move(changed);
  }
  void DismissHover();
  void CancelPointerInteraction();
  void ResetPointerState();
  void RefreshPointerHover();
  void SetText(UiTextView text);
  [[nodiscard]] bool FocusNative(TabId target);
  [[nodiscard]] bool IsPreviewOpen() const;
  // A sidebar drag holds the pointer (tab_list_drag.cpp).
  [[nodiscard]] bool IsDragging() const noexcept {
    return drag_.phase == DragPhase::kDragging;
  }
  // Ends any drag without dropping. Never throws: it runs from the pointer
  // handlers' catch blocks, where a second exception would reach XAML.
  void CancelDrag() noexcept;
  void AppendAnchors(std::vector<Anchor>& anchors, const ShellLayout& layout);
  void SetNewTabButton(xaml::FrameworkElement button) {
    new_tab_button_ = std::move(button);
  }
  [[nodiscard]] const controls::ListView& Root() const noexcept {
    return tabs_;
  }

 private:
  void BuildTabs();
  void SetHovered(TabRow& row, bool hovered);
  void LayoutHorizontalRows();
  void SizeHorizontalRow(TabRow& row);
  void ApplyRowContent(TabRow& row);
  void UpdateSplitRow(TabRow& row);
  void CloseWithPointer(const TabRow& row);
  void SetHoverWidth(float width);
  std::unique_ptr<TabRow> CreateTabRow(const controls::ListViewItem& container,
                                       const controls::Border& background,
                                       const controls::Grid& hit_target);
  void UpdateTabRow(TabRecord& record, bool animate_background = true);
  [[nodiscard]] static bool UpdateTabRecord(TabRecord& record,
                                            const TabItemVm& tab);
  void ReconcileTabs(const ShellViewModel& vm);
  void UpdateActiveTab(const ShellViewModel& vm);
  // Sidebar drags (tab_list_drag.cpp). Tabs, folders and Essentials move with
  // the pointer captured by the list rather than through system drag and
  // drop, whose modal session the browser's input routing cannot follow.
  enum class DragPhase : std::uint8_t { kIdle, kPressed, kDragging };
  enum class DragSource : std::uint8_t { kTab, kEssential, kFolder };
  struct DropTarget {
    enum class Kind : std::uint8_t {
      kNone,
      kList,        // between rows, before displayed_[position]
      kEssentials,  // the Essentials grid's `slot`
      kFolder,      // into `folder`
    };
    Kind kind = Kind::kNone;
    std::size_t position = 0;
    std::size_t slot = 0;
    FolderId folder;
    friend bool operator==(const DropTarget&, const DropTarget&) = default;
  };
  // A realized row along the list's axis, in ItemsPanelRoot coordinates.
  struct DragSlot {
    std::size_t index = 0;
    float start = 0, end = 0;
    Rect bounds;
  };
  struct DragState {
    DragPhase phase = DragPhase::kIdle;
    DragSource source = DragSource::kTab;
    TabId tab;
    FolderId folder;
    std::uint32_t pointer = 0;
    Point origin;  // in tabs_
    xaml::FrameworkElement element{nullptr};
    TabRow* row = nullptr;
    DropTarget target;
    FolderId highlighted;
  };
  void InstallDrag();
  void RemoveDrag() noexcept;
  void OnDragPressed(const xaml::Input::PointerRoutedEventArgs& args);
  void OnDragMoved(const xaml::Input::PointerRoutedEventArgs& args);
  void OnDragReleased(const xaml::Input::PointerRoutedEventArgs& args);
  [[nodiscard]] bool BeginDrag(const xaml::Input::PointerRoutedEventArgs& args);
  void UpdateDrag(const xaml::Input::PointerRoutedEventArgs& args);
  void ScrollForDrag(Point pointer);
  void MeasureDragSlots();
  [[nodiscard]] DropTarget ListTarget(Point point);
  [[nodiscard]] bool AcceptsFolderDrop(FolderId target) const;
  // `refresh` re-places the line after the realized rows were measured.
  void ShowDropTarget(const DropTarget& target, bool refresh);
  [[nodiscard]] Color IndicatorColor() const;
  void WatchScroll();
  void HighlightFolder(FolderId folder);
  void ShowDragTile(bool tile);
  void EndDrag(bool dropped);
  [[nodiscard]] TabDropSection SectionAt(std::size_t position) const;
  [[nodiscard]] std::pair<TabId, TabId> NeighboursAt(
      std::size_t position) const;
  [[nodiscard]] std::size_t FolderOrderAt(std::size_t position,
                                          FolderId parent) const;
  [[nodiscard]] std::optional<std::size_t> DisplayedIndex(
      const IInspectable& item) const;
  void UpdateFolderRow(FolderRecord& record);
  void ShowFolderMenu(TabRow& row);
  void RenameFolder(TabRow& row);
  void ShowClear(TabRow& row, bool today_header);
  // Removes the sidebar's New tab button from whichever row holds it.
  void DetachNewTabButton();
  void UpdateClear(TabRow& row);
  void UpdateClears();
  void SetListHovered(bool hovered);
  static bool SameItem(const IInspectable& a, const IInspectable& b);
  static controls::ScrollViewer FindScrollViewer(
      const xaml::DependencyObject& parent);
  media::ImageSource Bitmap(ImageId id) {
    return context_.images.Prepare(context_.delegate.OnImageRequested(id));
  }
  ViewContext& context_;
  bool& anchors_dirty_;
  FavoritesGrid favorites_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  controls::ListView tabs_;
  controls::ScrollViewer tab_scroll_{nullptr};
  controls::ScrollViewer::ViewChanged_revoker tab_scrolled_;
  foundation::Collections::IObservableVector<IInspectable> items_{nullptr};
  // Off-screen records retain only stable collection keys and row links.
  std::unordered_map<TabId, TabRecord> records_;
  std::unordered_map<FolderId, FolderRecord> folder_records_;
  ThemeVm folder_theme_;
  std::array<winrt::hstring, 10> folder_text_{};
  std::vector<std::pair<SpaceId, winrt::hstring>> folder_workspaces_;
  std::array<controls::ItemsPanelTemplate, 2> panels_{nullptr, nullptr};
  bool records_prepared_ = false;
  std::unordered_map<void*, std::unique_ptr<TabRow>> rows_;
  std::vector<IInspectable> desired_, displayed_;
  IInspectable favorites_header_ =
      winrt::box_value(winrt::hstring(L"Favorites"));
  IInspectable pinned_header_ = winrt::box_value(winrt::hstring(L"Pinned"));
  xaml::FrameworkElement new_tab_button_{nullptr};
  IInspectable new_tab_item_ =
      winrt::box_value(winrt::hstring(L"NewTabAction"));
  IInspectable today_header_ = winrt::box_value(winrt::hstring(L"Today"));
  std::uint64_t generation_ = 0;
  bool images_dirty_ = false;
  bool icon_only_ = false;
  bool horizontal_ = false;
  bool list_hovered_ = false;
  HorizontalTabWidths tab_widths_;
  std::move_only_function<void(float)> horizontal_extent_changed_;
  float hover_width_ = -1;
  // After a tab is closed with the pointer, the others keep their width until
  // the pointer leaves the list, so the next close button stays under it.
  std::optional<HorizontalTabWidths> held_widths_;
  bool today_idle_ = false;
  TabId scrolled_tab_;
  TabId active_tab_;
  bool rendered_ = false;
  TabPreviewMetrics preview_metrics_ =
      MeasureTabPreview(kTokens.tab_hover.width);
  winrt::hstring close_text_ = NativeText(UiText::kCloseTab);
  winrt::hstring mute_text_ = NativeText(UiText::kMuteTab);
  winrt::hstring unmute_text_ = NativeText(UiText::kUnmuteTab);
  std::array<winrt::hstring, 6> picker_labels_;
  winrt::hstring clear_text_ = NativeText(UiText::kClearTabs);
  winrt::hstring clear_help_text_ = NativeText(UiText::kClearTabsHelp);
  xaml::UIElement::PointerEntered_revoker list_entered_;
  xaml::UIElement::PointerExited_revoker list_exited_;
  xaml::FrameworkElement::SizeChanged_revoker sized_;
  controls::ListViewBase::ItemClick_revoker tab_click_;
  DragState drag_;
  std::vector<DragSlot> drag_slots_;
  bool drag_slots_dirty_ = true;
  std::unique_ptr<TabDragVisuals> drag_visuals_;
  xaml::Input::PointerEventHandler drag_pressed_{nullptr}, drag_moved_{nullptr},
      drag_released_{nullptr}, drag_lost_{nullptr};
  controls::ListViewBase::ContainerContentChanging_revoker containers_;
  foundation::TypedEventHandler<controls::ListViewBase,
                                controls::ContainerContentChangingEventArgs>
      realize_tab_{nullptr};
};

}  // namespace kr::win
