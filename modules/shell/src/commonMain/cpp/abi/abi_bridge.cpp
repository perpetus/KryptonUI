#include "boost_abi.hpp"
// The C boundary: exported entry points and the KrShellUI trampolines.
//
// Every trampoline is noexcept and catches everything: nothing may unwind into
// the host. Arguments are converted from C to owned C++ values here, because
// the host's pointers are only valid for the duration of the call.

#include <algorithm>
#include <cstring>
#include <exception>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "browser_tools_abi.hpp"
#include "folder_abi.hpp"
#include "glass_abi.hpp"
#include "krypton/ui/core/log.hpp"
#include "krypton/ui/model/mnemonic.hpp"
#include "krypton/ui/model/model.hpp"
#include "krypton/ui/platform/platform.hpp"
#include "krypton/ui/shell/shell.hpp"
#include "krypton/ui/shell/theme_contract.hpp"
#include "krypton_ui/abi.h"
#include "library_abi.hpp"
#include "menu_metadata.hpp"
#include "multi_page_abi.hpp"
#include "onboarding_abi.hpp"
#include "payload_array.hpp"
#include "privacy_features_abi.hpp"
#include "settings_abi.hpp"

namespace kr {
static_assert(std::to_underlying(SettingsSection::kAppearance) ==
              KR_SETTINGS_APPEARANCE);
static_assert(std::to_underlying(SettingsSection::kAbout) == KR_SETTINGS_ABOUT);
static_assert(std::to_underlying(ColorScheme::kDark) == KR_COLOR_DARK);
static_assert(std::to_underlying(MenuMaterialKind::kAcrylic) ==
              KR_MENU_MATERIAL_ACRYLIC);
static_assert(std::to_underlying(MenuMaterialKind::kAcrylicThin) ==
              KR_MENU_MATERIAL_ACRYLIC_THIN);
static_assert(std::to_underlying(AppearanceFlag::kHideHoverImages) ==
              KR_APPEARANCE_HIDE_HOVER_IMAGES);
static_assert(std::to_underlying(AppearanceFlag::kHideHoverMemory) ==
              KR_APPEARANCE_HIDE_HOVER_MEMORY);
static_assert(std::to_underlying(AppearanceFlag::kAttachedSearchBar) ==
              KR_APPEARANCE_ATTACHED_SEARCH_BAR);
static_assert(std::to_underlying(AppearanceFlag::kSearchBarFloatingOnType) ==
              KR_APPEARANCE_SEARCH_BAR_FLOATING_ON_TYPE);
static_assert(std::to_underlying(AppearanceFlag::kSearchBarMenuBackdrop) ==
              KR_APPEARANCE_SEARCH_BAR_MENU_BACKDROP);
static_assert(std::to_underlying(AppearanceFlag::kNewTabPage) ==
              KR_APPEARANCE_NEW_TAB_PAGE);

static_assert(std::to_underlying(LayoutFlag::kCompactTargetsExplicit) ==
              KR_LAYOUT_COMPACT_TARGETS_EXPLICIT);
static_assert(std::to_underlying(LayoutFlag::kWorkspaceSwipe) ==
              KR_LAYOUT_WORKSPACE_SWIPE);
static_assert(std::to_underlying(LayoutFlag::kTabsClassic) ==
              KR_LAYOUT_TABS_CLASSIC);
static_assert(std::to_underlying(LayoutFlag::kTabsCompact) ==
              KR_LAYOUT_TABS_COMPACT);
static_assert(std::to_underlying(LayoutFlag::kTabsDynamic) ==
              KR_LAYOUT_TABS_DYNAMIC);
static_assert(std::to_underlying(LayoutFlag::kCenteredAddress) ==
              KR_LAYOUT_CENTERED_ADDRESS);
static_assert(std::to_underlying(LayoutFlag::kMinimalAddress) ==
              KR_LAYOUT_MINIMAL_ADDRESS);
static_assert(std::to_underlying(LayoutFlag::kSquareFrame) ==
              KR_LAYOUT_SQUARE_FRAME);
namespace {

// The C enums and the C++ enums must never drift apart.
static_assert(std::to_underlying(TabFlag::kPinned) == KR_TAB_PINNED);
static_assert(std::to_underlying(TabFlag::kFavorite) == KR_TAB_FAVORITE);
static_assert(std::to_underlying(TabFlag::kLoading) == KR_TAB_LOADING);
static_assert(std::to_underlying(TabFlag::kAudible) == KR_TAB_AUDIBLE);
static_assert(std::to_underlying(TabFlag::kMuted) == KR_TAB_MUTED);
static_assert(std::to_underlying(TabFlag::kCrashed) == KR_TAB_CRASHED);
static_assert(std::to_underlying(TabFlag::kRecording) == KR_TAB_RECORDING);
static_assert(std::to_underlying(TabFlag::kCapturing) == KR_TAB_CAPTURING);
static_assert(std::to_underlying(TabFlag::kAttention) == KR_TAB_ATTENTION);
static_assert(std::to_underlying(TabPreviewFlag::kLabelsProvided) ==
              KR_TAB_PREVIEW_LABELS_PROVIDED);
static_assert(std::to_underlying(TabPreviewFlag::kNoImage) ==
              KR_TAB_PREVIEW_NO_IMAGE);
static_assert(std::to_underlying(SecurityLevel::kDangerous) ==
              KR_SECURITY_DANGEROUS);
static_assert(std::to_underlying(ActionKind::kToolbar) == KR_ACTION_TOOLBAR);
static_assert(std::to_underlying(ActionKind::kSiteTools) ==
              KR_ACTION_SITE_TOOLS);
static_assert(std::to_underlying(ActionKind::kCopyLink) == KR_ACTION_COPY_LINK);
static_assert(std::to_underlying(ActionFlag::kAttention) ==
              KR_ACTION_ATTENTION);
static_assert(std::to_underlying(WindowFlag::kTabFullscreen) ==
              KR_WINDOW_TAB_FULLSCREEN);
static_assert(std::to_underlying(LayoutFlag::kSidebarAutohide) ==
              KR_LAYOUT_SIDEBAR_AUTOHIDE);
static_assert(std::to_underlying(LayoutFlag::kToolbarSingle) ==
              KR_LAYOUT_TOOLBAR_SINGLE);
static_assert(std::to_underlying(LayoutFlag::kToolbarCollapsed) ==
              KR_LAYOUT_TOOLBAR_COLLAPSED);
static_assert(std::to_underlying(LayoutFlag::kSidebarIconOnly) ==
              KR_LAYOUT_SIDEBAR_ICON_ONLY);
static_assert(std::to_underlying(LayoutFlag::kCompactMode) ==
              KR_LAYOUT_COMPACT_MODE);
static_assert(std::to_underlying(LayoutFlag::kCompactHideSidebar) ==
              KR_LAYOUT_COMPACT_HIDE_SIDEBAR);
static_assert(std::to_underlying(LayoutFlag::kCompactHideToolbar) ==
              KR_LAYOUT_COMPACT_HIDE_TOOLBAR);
static_assert(std::to_underlying(SuggestionKind::kCommand) ==
              KR_SUGGESTION_COMMAND);
static_assert(std::to_underlying(MenuItemType::kTitle) == KR_MENU_ITEM_TITLE);
static_assert(std::to_underlying(MenuItemType::kControls) ==
              KR_MENU_ITEM_CONTROLS);
static_assert(std::to_underlying(MenuItemType::kGrid) == KR_MENU_ITEM_GRID);
static_assert(std::to_underlying(MenuItemType::kSection) ==
              KR_MENU_ITEM_SECTION);
static_assert(std::to_underlying(MenuItemType::kFooter) == KR_MENU_ITEM_FOOTER);
static_assert(std::to_underlying(NavCommand::kHome) == KR_NAV_HOME);
static_assert(std::to_underlying(Disposition::kNewWindow) ==
              KR_OPEN_NEW_WINDOW);
static_assert(std::to_underlying(WindowCommand::kClose) == KR_WINDOW_CLOSE);
static_assert(std::to_underlying(MenuKind::kSpace) == KR_MENU_SPACE);
static_assert(std::to_underlying(MenuKind::kSettings) == KR_MENU_SETTINGS);
static_assert(std::to_underlying(MenuKind::kTheme) == KR_MENU_THEME);
static_assert(std::to_underlying(MenuKind::kCreate) == KR_MENU_CREATE);
static_assert(std::to_underlying(MenuKind::kPage) == KR_MENU_PAGE);
static_assert(std::to_underlying(MenuKind::kSecurity) == KR_MENU_SECURITY);
static_assert(std::to_underlying(MenuKind::kDownload) == KR_MENU_DOWNLOAD);
static_assert(std::to_underlying(IconPack::kRadix) == KR_ICON_PACK_RADIX);
static_assert(std::to_underlying(MediaFlag::kCanToggleCamera) ==
              KR_MEDIA_CAN_TOGGLE_CAMERA);
static_assert(std::to_underlying(MenuKind::kLibraryItem) ==
              KR_MENU_LIBRARY_ITEM);
static_assert(std::to_underlying(Element::kCompactMode) ==
              KR_ELEMENT_COMPACT_MODE);
static_assert(std::to_underlying(AnchorKind::kTab) == KR_ANCHOR_TAB);
static_assert(std::to_underlying(Element::kSidebar) == KR_ELEMENT_SIDEBAR);
static_assert(std::to_underlying(Element::kToolbar) == KR_ELEMENT_TOOLBAR);
static_assert(std::to_underlying(Element::kSidebarToggle) ==
              KR_ELEMENT_SIDEBAR_TOGGLE);

// -------------------------------------------------------------- conversion

std::size_t live_shells = 0;

std::string ToString(KrStr text) {
  return text.data && text.size ? std::string(text.data, text.size)
                                : std::string();
}

std::string_view ToView(KrStr text) noexcept {
  return text.data ? std::string_view(text.data, text.size)
                   : std::string_view{};
}

Rect ToRect(const KrRect& r) {
  return {r.x, r.y, r.width, r.height};
}

Tab ToTab(const KrTab& tab) {
  return {
      .id = TabId{tab.id},
      .space = SpaceId{tab.space},
      .flags = Flags<TabFlag>::FromBits(tab.flags),
      .title = ToString(tab.title),
      .url = ToString(tab.url),
      .favicon = ImageId{tab.favicon},
      .folder = FolderId{KR_HAS_FIELD(&tab, KrTab, folder) ? tab.folder : 0},
  };
}

std::vector<Space> ToSpaces(const abi::PayloadArray<KrSpace>& spaces) {
  std::vector<Space> result;
  result.reserve(spaces.size());
  for (std::size_t i = 0; i < spaces.size(); ++i) {
    const auto space = spaces[i];
    result.push_back({
        .id = SpaceId{space.id},
        .name = ToString(space.name),
        .glyph = ToString(space.glyph),
        .color = Color{space.color},
    });
  }
  return result;
}

NavState ToNav(const KrNavState& nav) {
  return {
      .can_go_back = nav.can_go_back,
      .can_go_forward = nav.can_go_forward,
      .is_loading = nav.is_loading,
      .security = static_cast<SecurityLevel>(nav.security),
      .display_text = ToString(nav.display_text),
      .full_url = ToString(nav.full_url),
  };
}

std::vector<Action> ToActions(const abi::PayloadArray<KrAction>& actions) {
  std::vector<Action> result;
  result.reserve(actions.size());
  for (std::size_t i = 0; i < actions.size(); ++i) {
    const KrAction& a = actions[i];
    result.push_back({
        .id = ActionId{a.id},
        .kind = static_cast<ActionKind>(a.kind),
        .flags = Flags<ActionFlag>::FromBits(a.flags),
        .label = ToString(a.label),
        .tooltip = ToString(a.tooltip),
        .icon = ImageId{a.icon},
        .glyph = ToString(a.glyph),
        .badge = ToString(a.badge),
        .badge_color = Color{a.badge_color},
        .badge_text_color = Color{a.badge_text_color},
        .progress = a.progress,
    });
  }
  return result;
}

Result<std::vector<SuggestionClassification>> ToSuggestionClasses(
    const KrSuggestionClassification* values,
    std::size_t count,
    std::size_t stride,
    std::string_view text) {
  const auto input = abi::PayloadArray<KrSuggestionClassification>::Read(
      values, count, stride, sizeof(KrSuggestionClassification));
  if (!input)
    return std::unexpected(input.error());
  std::vector<SuggestionClassification> result;
  result.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto value = (*input)[i];
    if ((i == 0 && value.offset != 0) ||
        (i > 0 && value.offset <= result.back().offset) ||
        !AddressTextLength(text, AddressSelection{value.offset, value.offset}))
      return Fail(ErrorCode::kInvalidArgument,
                  "invalid suggestion classification");
    result.push_back(
        {value.offset, Flags<SuggestionTextStyle>::FromBits(value.style)});
  }
  return result;
}
Result<std::vector<Suggestion>> ToSuggestions(
    const abi::PayloadArray<KrSuggestion>& items) {
  std::vector<Suggestion> result;
  result.reserve(items.size());
  for (std::size_t i = 0; i < items.size(); ++i) {
    const KrSuggestion& s = items[i];
    const auto contents_text = ToString(s.contents);
    const auto description_text = ToString(s.description);
    auto contents =
        ToSuggestionClasses(s.contents_class, s.contents_class_count,
                            s.contents_class_stride, contents_text);
    auto description =
        ToSuggestionClasses(s.description_class, s.description_class_count,
                            s.description_class_stride, description_text);
    if (!contents || !description)
      return Fail(ErrorCode::kInvalidArgument,
                  "invalid suggestion text styles");
    result.push_back({
        .kind = static_cast<SuggestionKind>(s.kind),
        .is_default = (s.flags & KR_SUGGESTION_DEFAULT) != 0,
        .deletable = (s.flags & KR_SUGGESTION_DELETABLE) != 0,
        .contents = contents_text,
        .description = description_text,
        .icon = ImageId{s.icon},
        .selected = (s.flags & KR_SUGGESTION_SELECTED) != 0,
        .rich_image = (s.flags & KR_SUGGESTION_RICH_IMAGE) != 0,
        .contents_class = std::move(*contents),
        .description_class = std::move(*description),
    });
  }
  return result;
}

Result<MenuTree> ToMenuTree(const KrMenu* data,
                            std::size_t menu_count,
                            std::size_t element_size) {
  const auto menus = abi::PayloadArray<KrMenu>::Read(
      data, menu_count, element_size,
      offsetof(KrMenu, item_size) + sizeof(KrMenu::item_size));
  if (!menus)
    return std::unexpected(menus.error());
  MenuTree tree;
  tree.menus.resize(menu_count);
  std::vector<std::size_t> incoming(menu_count, 0);
  for (std::size_t m = 0; m < menu_count; ++m) {
    const auto menu = (*menus)[m];
    const auto rows = abi::PayloadArray<KrMenuItem>::Read(
        menu.items, menu.count, menu.item_size,
        offsetof(KrMenuItem, glyph) + sizeof(KrMenuItem::glyph));
    if (!rows)
      return std::unexpected(rows.error());
    auto& items = tree.menus[m];
    items.reserve(rows->size());
    for (std::size_t i = 0; i < rows->size(); ++i) {
      const auto item = (*rows)[i];
      if (item.type > KR_MENU_ITEM_FOOTER ||
          ((item.type == KR_MENU_ITEM_SUBMENU ||
            item.type == KR_MENU_ITEM_HEADER ||
            item.type == KR_MENU_ITEM_CONTROLS ||
            item.type == KR_MENU_ITEM_GRID ||
            item.type == KR_MENU_ITEM_SECTION ||
            item.type == KR_MENU_ITEM_FOOTER) &&
           item.submenu < 0))
        return Fail(ErrorCode::kInvalidArgument,
                    "invalid menu row type or child menu");
      if (item.submenu < -1 ||
          (item.submenu >= 0 &&
           static_cast<std::size_t>(item.submenu) >= menu_count))
        return Fail(ErrorCode::kInvalidArgument,
                    "submenu index is out of range");
      if (item.submenu >= 0)
        ++incoming[static_cast<std::size_t>(item.submenu)];
      auto metadata = abi::ReadMenuMetadata(item.metadata);
      if (!metadata)
        return std::unexpected(metadata.error());
      auto label = *metadata && (*metadata)->flags.Has(
                                    MenuMetadataFlag::kMayHaveMnemonics)
                       ? ParseMnemonic(ToView(item.label))
                       : MnemonicLabel{.text = ToString(item.label)};
      items.push_back({
          .command_id = item.command_id,
          .type = static_cast<MenuItemType>(item.type),
          .enabled = (item.flags & KR_MENU_ITEM_ENABLED) != 0,
          .checked = (item.flags & KR_MENU_ITEM_CHECKED) != 0,
          .keep_open = (item.flags & KR_MENU_ITEM_KEEP_OPEN) != 0,
          .label = std::move(label.text),
          .accelerator = ToString(item.accelerator),
          .access_key = std::move(label.access_key),
          .icon = ImageId{item.icon},
          .submenu = item.submenu,
          .glyph = ToString(item.glyph),
          .metadata = std::move(*metadata),
          .badge = ToString(item.badge),
          .badge_color = Color{item.badge_color},
      });
    }
  }
  // Topologically visit the flattened graph before native code builds nested
  // menus. A cycle could otherwise recurse forever in a platform renderer.
  std::vector<std::size_t> ready;
  ready.reserve(menu_count);
  for (std::size_t m = 0; m < menu_count; ++m) {
    if (incoming[m] == 0)
      ready.push_back(m);
  }
  for (std::size_t i = 0; i < ready.size(); ++i) {
    for (const MenuItem& item : tree.menus[ready[i]]) {
      if (item.submenu >= 0 &&
          --incoming[static_cast<std::size_t>(item.submenu)] == 0) {
        ready.push_back(static_cast<std::size_t>(item.submenu));
      }
    }
  }
  if (ready.size() != menu_count) {
    return Fail(ErrorCode::kInvalidArgument, "submenu graph contains a cycle");
  }
  return tree;
}

// ------------------------------------------------------------- trampolines

Shell& AsShell(void* self) {
  return *static_cast<Shell*>(self);
}

// Runs `body`, converting any exception into a log line.
template <typename Body>
void Guard(const char* entry, Body&& body) noexcept {
  try {
    std::forward<Body>(body)();
  } catch (const std::exception& e) {
    Log(LogLevel::kError, "{}: {}", entry, e.what());
  } catch (...) {
    Log(LogLevel::kError, "{}: unknown exception", entry);
  }
}

void FillShellUi(Shell* shell, KrShellUI& output) {
  const auto capacity = output.struct_size == 0
                            ? offsetof(KrShellUI, set_ui_text)
                            : output.struct_size;
  KrShellUI ui{};
  ui.struct_size = std::min(capacity, sizeof(KrShellUI));
  ui.self = shell;

  ui.destroy = [](void* self) noexcept {
    Guard("destroy", [&] {
      delete static_cast<Shell*>(self);
      --live_shells;
    });
  };
  ui.set_window_state = [](void* self, const KrWindowState* s) noexcept {
    if (!s || !KR_HAS_FIELD(s, KrWindowState, client))
      return;
    Guard("set_window_state", [&] {
      AsShell(self).SetWindowState({
          .flags = Flags<WindowFlag>::FromBits(s->flags),
          .scale_factor = s->scale_factor > 0 ? s->scale_factor : 1.0f,
          .client = ToRect(s->client),
      });
    });
  };
  ui.set_theme = [](void* self, const KrTheme* t) noexcept {
    if (!t || !KR_HAS_FIELD(t, KrTheme, accent))
      return;
    Guard("set_theme", [&] {
      AsShell(self).SetTheme({.dark = t->dark,
                              .incognito = t->incognito,
                              .high_contrast = t->high_contrast,
                              .accent = Color{t->accent}});
    });
  };
  ui.set_layout_prefs = [](void* self, const KrLayoutPrefs* p) noexcept {
    if (!p || !KR_HAS_FIELD(p, KrLayoutPrefs, sidebar_width))
      return;
    Guard("set_layout_prefs", [&] {
      AsShell(self).SetLayoutPrefs(
          {.flags = Flags<LayoutFlag>::FromBits(p->flags),
           .sidebar_width = p->sidebar_width});
    });
  };
  ui.put_image = [](void* self, const KrImage* image) noexcept {
    if (!image || !KR_HAS_FIELD(image, KrImage, pixels))
      return;
    Guard("put_image", [&] {
      AsShell(self).PutImage(ImageId{image->id}, image->width, image->height,
                             image->stride, image->pixels);
    });
  };
  ui.drop_image = [](void* self, KrImageId image) noexcept {
    Guard("drop_image", [&] { AsShell(self).DropImage(ImageId{image}); });
  };
  ui.set_spaces = [](void* self, const KrSpace* spaces, std::size_t count,
                     std::size_t element_size, KrSpaceId active) noexcept {
    Guard("set_spaces", [&] {
      const auto input = abi::PayloadArray<KrSpace>::Read(
          spaces, count, element_size,
          offsetof(KrSpace, color) + sizeof(KrSpace::color));
      if (!input) {
        Log(LogLevel::kWarning, "set_spaces ignored: {}",
            input.error().message);
        return;
      }
      AsShell(self).SetSpaces(ToSpaces(*input), SpaceId{active});
    });
  };
  ui.tab_inserted = [](void* self, std::size_t index,
                       const KrTab* tab) noexcept {
    if (!tab || !KR_HAS_FIELD(tab, KrTab, favicon))
      return;
    Guard("tab_inserted", [&] { AsShell(self).InsertTab(index, ToTab(*tab)); });
  };
  ui.tab_updated = [](void* self, const KrTab* tab) noexcept {
    if (!tab || !KR_HAS_FIELD(tab, KrTab, favicon))
      return;
    Guard("tab_updated", [&] { AsShell(self).UpdateTab(ToTab(*tab)); });
  };
  ui.tab_moved = [](void* self, KrTabId tab, std::size_t to_index) noexcept {
    Guard("tab_moved", [&] { AsShell(self).MoveTab(TabId{tab}, to_index); });
  };
  ui.tab_removed = [](void* self, KrTabId tab) noexcept {
    Guard("tab_removed", [&] { AsShell(self).RemoveTab(TabId{tab}); });
  };
  ui.set_active_tab = [](void* self, KrTabId tab) noexcept {
    Guard("set_active_tab", [&] { AsShell(self).SetActiveTab(TabId{tab}); });
  };
  ui.set_nav_state = [](void* self, const KrNavState* nav) noexcept {
    if (!nav || !KR_HAS_FIELD(nav, KrNavState, full_url))
      return;
    Guard("set_nav_state", [&] { AsShell(self).SetNav(ToNav(*nav)); });
  };
  ui.set_actions = [](void* self, const KrAction* actions, std::size_t count,
                      std::size_t element_size) noexcept {
    Guard("set_actions", [&] {
      const auto input = abi::PayloadArray<KrAction>::Read(
          actions, count, element_size,
          offsetof(KrAction, progress) + sizeof(KrAction::progress));
      if (!input) {
        Log(LogLevel::kWarning, "set_actions ignored: {}",
            input.error().message);
        return;
      }
      AsShell(self).SetActions(ToActions(*input));
    });
  };
  ui.set_suggestions = [](void* self, std::uint32_t request_id,
                          const KrSuggestion* items, std::size_t count,
                          std::size_t element_size) noexcept {
    Guard("set_suggestions", [&] {
      const auto input = abi::PayloadArray<KrSuggestion>::Read(
          items, count, element_size,
          offsetof(KrSuggestion, icon) + sizeof(KrSuggestion::icon));
      if (!input) {
        Log(LogLevel::kWarning, "set_suggestions ignored: {}",
            input.error().message);
        return;
      }
      auto converted = ToSuggestions(*input);
      if (converted)
        AsShell(self).SetSuggestions(request_id, std::move(*converted));
    });
  };
  ui.show_menu = [](void* self, std::uint32_t request_id, const KrMenu* menus,
                    std::size_t menu_count, std::size_t element_size) noexcept {
    Guard("show_menu", [&] {
      auto menu = ToMenuTree(menus, menu_count, element_size);
      if (!menu) {
        Log(LogLevel::kWarning, "show_menu ignored: {}", menu.error().message);
        return;
      }
      AsShell(self).ShowMenu(request_id, *menu);
    });
  };
  ui.focus_address = [](void* self, bool select_all) noexcept {
    Guard("focus_address", [&] { AsShell(self).FocusAddress(select_all); });
  };
  ui.pre_translate_message = [](void* self,
                                const void* native_message) noexcept {
    bool handled = false;
    Guard("pre_translate_message",
          [&] { handled = AsShell(self).PreTranslateMessage(native_message); });
    return handled;
  };
  ui.set_ui_text = [](void* self, KrStr key, KrStr text) noexcept {
    KrResult result = KR_ERR_PLATFORM;
    Guard("set_ui_text", [&] {
      result = AsShell(self).SetUiText(ToView(key), ToView(text))
                   ? KR_OK
                   : KR_ERR_INVALID_ARGUMENT;
    });
    return result;
  };
  ui.set_text_direction = [](void* self, bool right_to_left) noexcept {
    Guard("set_text_direction",
          [&] { AsShell(self).SetTextDirection(right_to_left); });
  };
  ui.show_menu_with_glyphs =
      [](void* self, std::uint32_t request_id, const KrMenuWithGlyphs* menus,
         std::size_t count, std::size_t element_size) noexcept {
        Guard("show_menu_with_glyphs", [&] {
          auto menu = ToMenuTree(menus, count, element_size);
          if (!menu) {
            Log(LogLevel::kWarning, "show_menu_with_glyphs ignored: {}",
                menu.error().message);
            return;
          }
          AsShell(self).ShowMenu(request_id, *menu);
        });
      };
  ui.request_context_menu = [](void* self, std::uint32_t kind,
                               std::uint64_t target, KrRect anchor) noexcept {
    Guard("request_context_menu", [&] {
      if (kind <= KR_MENU_SECURITY)
        AsShell(self).OnMenuRequested(static_cast<MenuKind>(kind), target,
                                      ToRect(anchor));
    });
  };
  ui.set_tab_preview = [](void* self, std::uint32_t request_id,
                          const KrTabPreview* preview) noexcept {
    if (!preview ||
        preview->struct_size <
            offsetof(KrTabPreview, flags) + sizeof(preview->flags) ||
        !preview->tab)
      return KR_ERR_INVALID_ARGUMENT;
    const bool labels = (preview->flags & KR_TAB_PREVIEW_LABELS_PROVIDED) != 0;
    if (labels && (!KR_HAS_FIELD(preview, KrTabPreview, status) ||
                   (!preview->title.data && preview->title.size) ||
                   (!preview->domain.data && preview->domain.size) ||
                   (!preview->memory_text.data && preview->memory_text.size) ||
                   (!preview->status.data && preview->status.size)))
      return KR_ERR_INVALID_ARGUMENT;
    KrResult result = KR_ERR_PLATFORM;
    Guard("set_tab_preview", [&] {
      AsShell(self).SetTabPreview(
          request_id, TabId{preview->tab},
          {.thumbnail = ImageId{preview->thumbnail},
           .memory_usage_bytes = preview->memory_usage_bytes,
           .flags = Flags<TabPreviewFlag>::FromBits(preview->flags),
           .title = labels ? ToString(preview->title) : std::string{},
           .domain = labels ? ToString(preview->domain) : std::string{},
           .memory_text =
               labels ? ToString(preview->memory_text) : std::string{},
           .status = labels ? ToString(preview->status) : std::string{}});
      result = KR_OK;
    });
    return result;
  };
  ui.set_workspace_theme = [](void* self,
                              const KrWorkspaceTheme* input) noexcept {
    KrResult result = KR_ERR_INVALID_ARGUMENT;
    Guard("set_workspace_theme", [&] {
      auto theme = FromThemeContract(input);
      if (theme)
        result = AsShell(self).SetWorkspaceTheme(SpaceId{input->space}, *theme)
                     ? KR_OK
                     : KR_ERR_INVALID_ARGUMENT;
    });
    return result;
  };
  ui.show_theme_editor = [](void* self, KrSpaceId space) noexcept {
    KrResult result = KR_ERR_PLATFORM;
    Guard("show_theme_editor", [&] {
      const auto shown = AsShell(self).ShowThemeEditor(SpaceId{space});
      result = shown ? KR_OK
               : shown.error().code == ErrorCode::kUnsupported
                   ? KR_ERR_UNSUPPORTED
                   : KR_ERR_INVALID_ARGUMENT;
    });
    return result;
  };
  ui.show_menu_panel = [](void* self, std::uint32_t request_id,
                          const KrMenuWithGlyphs* menus, std::size_t count,
                          std::size_t element_size) noexcept {
    Guard("show_menu_panel", [&] {
      auto menu = ToMenuTree(menus, count, element_size);
      if (!menu) {
        Log(LogLevel::kWarning, "show_menu_panel ignored: {}",
            menu.error().message);
        return;
      }
      menu->panel = true;
      AsShell(self).ShowMenu(request_id, *menu);
    });
  };
  ui.set_load_progress = [](void* self, const KrLoadProgress* input) noexcept {
    if (!input || input->struct_size <
                      offsetof(KrLoadProgress, flags) + sizeof(input->flags))
      return KR_ERR_INVALID_ARGUMENT;
    KrResult result = KR_ERR_PLATFORM;
    Guard("set_load_progress", [&] {
      const auto changed = AsShell(self).SetLoadProgress(
          {.tab = TabId{input->tab},
           .navigation_id = input->navigation_id,
           .progress = input->progress,
           .loading = (input->flags & KR_LOAD_PROGRESS_LOADING) != 0,
           .enabled = (input->flags & KR_LOAD_PROGRESS_ENABLED) != 0,
           .same_document =
               (input->flags & KR_LOAD_PROGRESS_SAME_DOCUMENT) != 0});
      result = changed ? KR_OK : KR_ERR_INVALID_ARGUMENT;
    });
    return result;
  };
  ui.set_appearance_prefs = [](void* self,
                               const KrAppearancePrefs* input) noexcept {
    if (!input || !KR_HAS_FIELD(input, KrAppearancePrefs, flags) ||
        input->color_scheme > KR_COLOR_DARK ||
        input->menu_material > KR_MENU_MATERIAL_ACRYLIC_THIN)
      return KR_ERR_INVALID_ARGUMENT;
    const bool icon_pack_supplied =
        KR_HAS_FIELD(input, KrAppearancePrefs, icon_pack);
    const std::uint64_t icon_pack = icon_pack_supplied
                                        ? input->icon_pack
                                        : std::uint32_t{KR_ICON_PACK_FLUENT};
    if (icon_pack > KR_ICON_PACK_RADIX)
      return KR_ERR_INVALID_ARGUMENT;
    const auto glass_mode = KR_HAS_FIELD(input, KrAppearancePrefs, glass_mode)
                                ? input->glass_mode
                                : uint64_t{KR_GLASS_OFF};
    if (glass_mode > KR_GLASS_QUALITY)
      return KR_ERR_INVALID_ARGUMENT;
    KrResult result = KR_ERR_PLATFORM;
    Guard("set_appearance_prefs", [&] {
      const auto changed = AsShell(self).SetAppearancePrefs(
          {.scheme = static_cast<ColorScheme>(input->color_scheme),
           .menu_material = static_cast<MenuMaterialKind>(input->menu_material),
           .flags = Flags<AppearanceFlag>::FromBits(
               input->flags & (KR_APPEARANCE_HIDE_HOVER_IMAGES |
                               KR_APPEARANCE_HIDE_HOVER_MEMORY |
                               KR_APPEARANCE_ATTACHED_SEARCH_BAR |
                               KR_APPEARANCE_SEARCH_BAR_FLOATING_ON_TYPE |
                               KR_APPEARANCE_SEARCH_BAR_MENU_BACKDROP |
                               KR_APPEARANCE_NEW_TAB_PAGE)),
           .icon_pack = static_cast<IconPack>(icon_pack),
           .glass_mode = static_cast<GlassMode>(glass_mode)},
          icon_pack_supplied);
      result = changed ? KR_OK : KR_ERR_INVALID_ARGUMENT;
    });
    return result;
  };
  ui.show_settings = [](void* self, std::uint32_t section) noexcept {
    if (section > KR_SETTINGS_CURRENT)
      return KR_ERR_INVALID_ARGUMENT;
    KrResult result = KR_ERR_PLATFORM;
    Guard("show_settings", [&] {
      const auto shown = AsShell(self).ShowSettings(
          section == KR_SETTINGS_CURRENT
              ? std::nullopt
              : std::optional{static_cast<SettingsSection>(section)});
      result = shown ? KR_OK : KR_ERR_INVALID_ARGUMENT;
    });
    return result;
  };
  ui.set_address_text = [](void* self, KrStr text, bool select_all) noexcept {
    if (!text.data && text.size)
      return KR_ERR_INVALID_ARGUMENT;
    KrResult result = KR_ERR_PLATFORM;
    Guard("set_address_text", [&] {
      AsShell(self).SetAddressText(ToString(text), select_all);
      result = KR_OK;
    });
    return result;
  };
  ui.dismiss_address = [](void* self) noexcept {
    Guard("dismiss_address", [&] { AsShell(self).DismissAddress(); });
  };
  ui.focus_native = [](void* self, const KrNativeFocus* target) noexcept {
    if (!target || !KR_HAS_FIELD(target, KrNativeFocus, id) ||
        target->kind > KR_ANCHOR_TAB)
      return KR_ERR_INVALID_ARGUMENT;
    KrResult result = KR_ERR_PLATFORM;
    Guard("focus_native", [&] {
      const auto focused = AsShell(self).FocusNative(
          {.kind = static_cast<AnchorKind>(target->kind), .id = target->id});
      result = focused ? KR_OK : KR_ERR_INVALID_ARGUMENT;
    });
    return result;
  };
  ui.set_address_edit = [](void* self, const KrAddressEdit* edit) noexcept {
    if (!edit || !KR_HAS_SLOT(edit, KrAddressEdit, selection_end) ||
        (!edit->text.data && edit->text.size))
      return KR_ERR_INVALID_ARGUMENT;
    KrResult result = KR_ERR_PLATFORM;
    Guard("set_address_edit", [&] {
      const auto updated = AsShell(self).SetAddressEdit(
          edit->request_id, edit->edit_revision, ToString(edit->text),
          {edit->selection_start, edit->selection_end, false});
      result = updated ? KR_OK : KR_ERR_INVALID_ARGUMENT;
    });
    return result;
  };
  ui.set_suggestion_snapshot = [](void* self, std::uint32_t request,
                                  std::uint64_t generation,
                                  const KrSuggestion* items, std::size_t count,
                                  std::size_t stride) noexcept {
    KrResult result = KR_ERR_PLATFORM;
    Guard("set_suggestion_snapshot", [&] {
      const auto input = abi::PayloadArray<KrSuggestion>::Read(
          items, count, stride,
          offsetof(KrSuggestion, icon) + sizeof(KrSuggestion::icon));
      if (!input) {
        result = KR_ERR_INVALID_ARGUMENT;
        return;
      }
      auto converted = ToSuggestions(*input);
      if (!converted) {
        result = KR_ERR_INVALID_ARGUMENT;
        return;
      }
      const auto changed = AsShell(self).SetSuggestionSnapshot(
          request, generation, std::move(*converted));
      result = changed ? KR_OK : KR_ERR_INVALID_ARGUMENT;
    });
    return result;
  };
  ui.dismiss_menu = [](void* self, std::uint32_t request) noexcept {
    Guard("dismiss_menu", [&] { AsShell(self).DismissMenu(request); });
  };
  ui.show_boost_editor = [](void* self, const KrBoost* input) noexcept {
    KrResult result = KR_ERR_INVALID_ARGUMENT;
    Guard("show_boost_editor", [&] {
      if (auto draft = abi::ReadBoost(input))
        result = AsShell(self).ShowBoostEditor(std::move(*draft))
                     ? KR_OK
                     : KR_ERR_INVALID_ARGUMENT;
    });
    return result;
  };
  ui.show_tab_icon_editor = [](void* self, KrTabId tab, KrStr glyph) noexcept {
    KrResult result = KR_ERR_PLATFORM;
    Guard("show_tab_icon_editor", [&] {
      if ((!glyph.data && glyph.size) || glyph.size > 128) {
        result = KR_ERR_INVALID_ARGUMENT;
        return;
      }
      const auto shown = AsShell(self).ShowTabIconEditor(
          TabId{tab}, glyph.data ? std::string_view(glyph.data, glyph.size)
                                 : std::string_view{});
      result = shown ? KR_OK : KR_ERR_INVALID_ARGUMENT;
    });
    return result;
  };
  ui.show_workspace_editor = [](void* self, KrSpaceId space) noexcept {
    KrResult result = KR_ERR_PLATFORM;
    Guard("show_workspace_editor", [&] {
      const auto shown = AsShell(self).ShowWorkspaceEditor(SpaceId{space});
      result = shown ? KR_OK
               : shown.error().code == ErrorCode::kUnsupported
                   ? KR_ERR_UNSUPPORTED
                   : KR_ERR_INVALID_ARGUMENT;
    });
    return result;
  };
  ui.show_menu_panel_with_tiles = ui.show_menu_panel;
  abi::FillBrowserToolsUi(ui);
  abi::FillPrivacyFeaturesUi(ui);
  abi::FillLibraryUi(ui);
  abi::FillOnboardingUi(ui);
  abi::FillSettingsUi(ui);
  abi::FillMultiPageUi(ui);
  abi::FillGlassUi(ui);
  abi::FillFolderUi(ui);
  ui.dismiss_site_permissions = [](void* self) noexcept {
    Guard("dismiss_site_permissions",
          [&] { AsShell(self).OnSitePermissionsDismissed(); });
  };
  ui.url_drag_changed = [](void* self, float x, float y, bool active) noexcept {
    bool accepted = false;
    Guard("url_drag_changed",
          [&] { accepted = AsShell(self).UrlDragChanged({x, y}, active); });
    return accepted;
  };
  std::memcpy(&output, &ui, ui.struct_size);
}

KrResult ToKrResult(ErrorCode code) {
  switch (code) {
    case ErrorCode::kVersionMismatch:
      return KR_ERR_VERSION;
    case ErrorCode::kUnsupported:
      return KR_ERR_UNSUPPORTED;
    case ErrorCode::kInvalidArgument:
    case ErrorCode::kNotFound:
      return KR_ERR_INVALID_ARGUMENT;
    case ErrorCode::kPlatform:
      return KR_ERR_PLATFORM;
  }
  return KR_ERR_PLATFORM;
}

}  // namespace
}  // namespace kr

extern "C" {

KR_UI_API bool KR_UI_CALL kr_ui_is_supported(uint32_t abi_version) {
  return abi_version == KR_UI_ABI_VERSION &&
         kr::platform::CheckSupport().has_value();
}

KR_UI_API KrResult KR_UI_CALL kr_ui_create(const KrCreateParams* params,
                                           KrShellUI* out_ui) {
  using namespace kr;
  if (params == nullptr || out_ui == nullptr ||
      (out_ui->struct_size != 0 &&
       out_ui->struct_size < offsetof(KrShellUI, set_ui_text)) ||
      params->struct_size <
          offsetof(KrCreateParams, browser) + sizeof(params->browser) ||
      params->browser == nullptr ||
      params->browser->struct_size <
          offsetof(KrBrowser, self) + sizeof(void*)) {
    return KR_ERR_INVALID_ARGUMENT;
  }
  if (params->abi_version != KR_UI_ABI_VERSION)
    return KR_ERR_VERSION;

  KrResult result = KR_ERR_PLATFORM;
  Guard("kr_ui_create", [&] {
    if (auto support = platform::CheckSupport(); !support) {
      Log(LogLevel::kWarning, "UI unsupported here: {}",
          support.error().message);
      result = KR_ERR_UNSUPPORTED;
      return;
    }
    Theme initial;
    if (KR_HAS_FIELD(params, KrCreateParams, initial_theme) &&
        params->initial_theme) {
      const auto& theme = *params->initial_theme;
      if (!KR_HAS_FIELD(&theme, KrTheme, accent)) {
        result = KR_ERR_INVALID_ARGUMENT;
        return;
      }
      initial = {.dark = theme.dark,
                 .incognito = theme.incognito,
                 .high_contrast = theme.high_contrast,
                 .accent = Color{theme.accent}};
    }
    auto shell =
        Shell::Create(params->parent_window, *params->browser, initial);
    if (!shell) {
      // Failed creation destroys the temporary Shell and clears its log sink.
      // Report directly through the validated host table so startup failures
      // remain visible to the browser rather than only the platform debugger.
      const BrowserPort browser(*params->browser);
      const auto message =
          std::format("UI creation failed: {}", shell.error().message);
      if (browser.can_log()) {
        browser.Log(LogLevel::kError, message);
      } else {
        Log(LogLevel::kError, "{}", message);
      }
      result = ToKrResult(shell.error().code);
      return;
    }
    FillShellUi(shell->release(), *out_ui);
    ++live_shells;
    result = KR_OK;
  });
  return result;
}

KR_UI_API KrResult KR_UI_CALL kr_ui_shutdown(void) {
  using namespace kr;
  if (live_shells != 0)
    return KR_ERR_INVALID_ARGUMENT;
  KrResult result = KR_ERR_PLATFORM;
  Guard("kr_ui_shutdown", [&] {
    const auto shutdown = platform::ShutdownRuntime();
    result = shutdown ? KR_OK : ToKrResult(shutdown.error().code);
  });
  return result;
}

KR_UI_API const char* KR_UI_CALL kr_ui_platform_name(void) {
  static const std::string name(kr::platform::Name());
  return name.c_str();
}

}  // extern "C"
