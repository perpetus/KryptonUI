#pragma once

#include <unordered_map>

#include "address_progress.hpp"
#include "sprite_icon.hpp"
#include "view_context.hpp"

namespace kr::win {

class ChromeHeader {
 public:
  explicit ChromeHeader(ViewContext& context, bool& anchors_dirty);
  void SetSingleToolbar(bool single);
  void SetLayout(const ShellLayout& layout);
  [[nodiscard]] bool RefreshPlacement() { return PlaceAddress(); }
  [[nodiscard]] const controls::Grid& TabSlot() const noexcept {
    return tab_slot_;
  }
  void Render(const ShellViewModel& vm);
  void SetText(UiTextView text);
  [[nodiscard]] bool FocusNative(Anchor target);
  void AppendAnchors(std::vector<Anchor>& anchors,
                     bool visible,
                     bool sidebar_visible) const;
  void AppendDragExclusions(std::vector<Rect>& controls,
                            const ShellLayout& layout) const;

  [[nodiscard]] const controls::Grid& SidebarTop() const noexcept {
    return sidebar_top_;
  }
  [[nodiscard]] const controls::Grid& AddressRow() const noexcept {
    return address_row_;
  }
  [[nodiscard]] const controls::Grid& ActionsRow() const noexcept {
    return actions_row_;
  }
  [[nodiscard]] const controls::Grid& Toolbar() const noexcept {
    return toolbar_root_;
  }
  [[nodiscard]] const controls::Grid& LibrarySlot() const noexcept {
    return library_slot_;
  }

  [[nodiscard]] const controls::Grid& MediaSlot() const noexcept {
    return media_slot_;
  }

 private:
  bool PlaceAddress();
  void PlaceAddressActions();
  // An action's icon with its badge drawn over the icon's top end. Created on
  // the first badge and kept, collapsed, if the badge later clears.
  struct ActionBadge {
    controls::Grid root{nullptr};
    controls::Border badge{nullptr};
    controls::TextBlock text{nullptr};
  };
  struct ToolsVisual {
    controls::Grid root{nullptr};
    xaml::UIElement normal{nullptr}, boosted{nullptr};
    std::unique_ptr<SpriteIcon> indicator;
    bool active = false;
    xaml::FrameworkElement::Loaded_revoker loaded;
    xaml::FrameworkElement::Unloaded_revoker unloaded;
  };
  void RefreshToolsVisuals();
  void ReconcileActions(
      std::span<const ActionVm> actions,
      controls::StackPanel panel,
      std::unordered_map<ActionId, std::unique_ptr<NativeButton>>& buttons);
  [[nodiscard]] xaml::UIElement ActionIcon(const NativeButton& button,
                                           ActionId id) const;
  void ShowActionIcon(NativeButton& button,
                      const ActionVm& action,
                      const xaml::UIElement& icon);
  void ContextFor(NativeButton& button, MenuKind kind);
  media::ImageSource Bitmap(ImageId id) {
    return context_.images.Get(id, images_);
  }

  ViewContext& context_;
  controls::Grid address_slot_, tab_slot_;
  bool compact_tabs_ = false, centered_address_ = false,
       placing_address_ = false;
  xaml::FrameworkElement::SizeChanged_revoker toolbar_sized_, slot_sized_,
      end_sized_;
  bool& anchors_dirty_;
  std::vector<ActionId> page_action_order_;
  std::vector<ActionKind> page_action_kinds_;
  std::vector<std::uint8_t> page_action_visibility_;
  controls::Grid sidebar_top_, address_row_, actions_row_, toolbar_root_,
      address_host_, address_content_, progress_layer_;
  AddressProgress progress_;
  media::RotateTransform app_menu_rotation_;
  controls::Grid library_slot_, media_slot_;
  controls::StackPanel leading_, navigation_, window_controls_, end_group_,
      toolbar_, page_actions_;
  controls::Button address_;
  controls::TextBlock address_text_{nullptr}, security_{nullptr};
  std::unique_ptr<NativeButton> back_, forward_, reload_, minimize_, maximize_,
      close_, toggle_, app_menu_, security_button_;
  std::unordered_map<ActionId, std::unique_ptr<NativeButton>> page_buttons_,
      toolbar_buttons_;
  std::unordered_map<ActionId, ActionBadge> action_badges_;
  std::unordered_map<ActionId, ToolsVisual> tools_visuals_;
  bool window_active_ = false, chrome_visible_ = true;
  std::uint64_t button_generation_ = 0;
  const ImageStore* images_ = nullptr;
  bool single_ = false;
  double row_height_ = kTokens.spacing.header_height;
  bool initialized_ = false;
  bool loading_ = false;
  bool maximized_ = false;
  bool rendered_ = false;
  bool window_controls_visible_ = true;
  bool show_site_security_ = true;
  std::string address_value_;
  winrt::hstring reload_text_, stop_text_, maximize_text_, restore_text_;
  winrt::hstring address_placeholder_;
  controls::Button::Click_revoker address_click_;
  xaml::FrameworkElement::SizeChanged_revoker address_sized_;
  xaml::UIElement::GotFocus_revoker address_focused_;
  xaml::UIElement::LostFocus_revoker address_blurred_;
};

}  // namespace kr::win
