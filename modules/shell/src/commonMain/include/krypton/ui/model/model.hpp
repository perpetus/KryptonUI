#pragma once

// Plain data the browser pushes into the UI. Enum values deliberately match
// abi.h so the ABI bridge converts with a cast; it static_asserts that they
// stay in sync.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "krypton/ui/core/color.hpp"
#include "krypton/ui/core/flags.hpp"
#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/core/ids.hpp"
#include "krypton/ui/model/folder.hpp"
#include "krypton/ui/model/menu_metadata.hpp"
#include "krypton/ui/model/workspace_theme.hpp"

namespace kr {

// ------------------------------------------------------------------ tabs --

enum class TabFlag : std::uint32_t {
  kPinned = 1u << 0,
  kFavorite = 1u << 1,
  kLoading = 1u << 2,
  kAudible = 1u << 3,
  kMuted = 1u << 4,
  kCrashed = 1u << 5,
  kRecording = 1u << 6,
  kCapturing = 1u << 7,
  kAttention = 1u << 8,
};
template <>
inline constexpr bool kIsFlagEnum<TabFlag> = true;

struct Tab {
  TabId id;
  SpaceId space;
  Flags<TabFlag> flags;
  std::string title;
  std::string url;
  ImageId favicon;
  FolderId folder;
};

enum class TabPreviewFlag : std::uint32_t {
  kMemoryKnown = 1u << 0,
  kSleeping = 1u << 1,
  kSample = 1u << 2,
  kMemoryEstimated = 1u << 3,
  kLabelsProvided = 1u << 4,
  kNoImage = 1u << 5,
};
template <>
inline constexpr bool kIsFlagEnum<TabPreviewFlag> = true;

struct TabPreviewData {
  ImageId thumbnail;
  std::uint64_t memory_usage_bytes = 0;
  Flags<TabPreviewFlag> flags;
  std::string title, domain, memory_text, status;
  friend bool operator==(const TabPreviewData&,
                         const TabPreviewData&) = default;
};

struct Space {
  SpaceId id;
  std::string name;
  std::string glyph;
  Color color;
};

// ------------------------------------------------------------ navigation --

enum class SecurityLevel : std::uint32_t {
  kNone = 0,
  kSecure = 1,
  kInfo = 2,
  kWarning = 3,
  kDangerous = 4,
};

struct NavState {
  bool can_go_back = false;
  bool can_go_forward = false;
  bool is_loading = false;
  SecurityLevel security = SecurityLevel::kNone;
  std::string display_text;
  std::string full_url;
};

// --------------------------------------------------------------- actions --

enum class ActionKind : std::uint32_t {
  kPage = 0,
  kSiteSetting = 1,
  kExtension = 2,
  kPinned = 3,
  kToolbar = 4,
  kSiteTools = 5,
  kCopyLink = 6,
};

enum class ActionFlag : std::uint32_t {
  kVisible = 1u << 0,
  kEnabled = 1u << 1,
  kActive = 1u << 2,
  kAttention = 1u << 3,
};
template <>
inline constexpr bool kIsFlagEnum<ActionFlag> = true;

struct Action {
  ActionId id;
  ActionKind kind = ActionKind::kPage;
  Flags<ActionFlag> flags;
  std::string label;
  std::string tooltip;
  ImageId icon;
  std::string glyph;
  std::string badge;
  Color badge_color;
  Color badge_text_color;  // transparent: the default badge text colour
  float progress = -1;     // < 0: no progress ring
};

// ---------------------------------------------------------- window/theme --

struct Theme {
  bool dark = false;
  bool incognito = false;
  bool high_contrast = false;
  Color accent;
};

enum class WindowFlag : std::uint32_t {
  kActive = 1u << 0,
  kMaximized = 1u << 1,
  kFullscreen = 1u << 2,
  kTabFullscreen = 1u << 3,
};
template <>
inline constexpr bool kIsFlagEnum<WindowFlag> = true;

struct WindowState {
  Flags<WindowFlag> flags;
  float scale_factor = 1;
  Rect client;
};

struct NativeTitleBarMetrics {
  bool enabled = false;
  float height = 0;
  float left_inset = 0;
  float right_inset = 0;
  friend bool operator==(const NativeTitleBarMetrics&,
                         const NativeTitleBarMetrics&) = default;
};

enum class LayoutFlag : std::uint32_t {
  kSidebarRight = 1u << 0,
  kSidebarCollapsed = 1u << 1,
  kSidebarAutohide = 1u << 2,
  kToolbarSingle = 1u << 3,
  kToolbarCollapsed = 1u << 4,
  kSidebarIconOnly = 1u << 5,
  kCompactMode = 1u << 6,
  kCompactHideSidebar = 1u << 7,
  kCompactHideToolbar = 1u << 8,
  kTabsClassic = 1u << 9,
  kTabsCompact = 1u << 10,
  kTabsDynamic = 1u << 11,
  kCenteredAddress = 1u << 12,
  kMinimalAddress = 1u << 13,
  kSquareFrame = 1u << 14,
  kCompactTargetsExplicit = 1u << 15,
  kWorkspaceSwipe = 1u << 16,
};
template <>
inline constexpr bool kIsFlagEnum<LayoutFlag> = true;

struct LayoutPrefs {
  Flags<LayoutFlag> flags;
  float sidebar_width = 0;  // 0: use the token default
  friend bool operator==(const LayoutPrefs&, const LayoutPrefs&) = default;
};

// Values follow the reference layout controller. Sidebar presentation remains
// independent, so switching orientation preserves the user's vertical layout.
enum class TabLayout {
  kClassic = 0,
  kCompact = 1,
  kVertical = 2,
  kDynamic = 3
};

// Three vertical window layouts. Compact mode independently hides either bar.
enum class ToolbarLayout {
  kMultiple,   // Sidebar and Top Toolbar
  kSingle,     // Only Sidebar
  kCollapsed,  // Collapsed Sidebar (icon rail and top toolbar)
};

enum class CompactMode { kOff, kHideSidebar, kHideToolbar, kHideBoth };

// How the tab sidebar presents itself: full width, as an icon rail, or hidden
// until the pointer reaches the window edge.
enum class SidebarMode { kExpanded, kCollapsed, kFloating };

// ----------------------------------------------------------- suggestions --

enum class SuggestionKind : std::uint32_t {
  kUrl = 0,
  kSearch = 1,
  kHistory = 2,
  kBookmark = 3,
  kOpenTab = 4,
  kBang = 5,
  kCommand = 6,
};

enum class SuggestionTextStyle : std::uint32_t {
  kUrl = 1u << 0,
  kMatch = 1u << 1,
  kDim = 1u << 2,
  kToolbelt = 1u << 3,
};
template <>
inline constexpr bool kIsFlagEnum<SuggestionTextStyle> = true;
struct SuggestionClassification {
  std::uint32_t offset = 0;
  Flags<SuggestionTextStyle> style;
  friend bool operator==(const SuggestionClassification&,
                         const SuggestionClassification&) = default;
};
struct Suggestion {
  SuggestionKind kind = SuggestionKind::kUrl;
  bool is_default = false;
  bool deletable = false;
  std::string contents;
  std::string description;
  ImageId icon;
  bool selected = false;
  bool rich_image = false;
  std::vector<SuggestionClassification> contents_class, description_class;
};

// ----------------------------------------------------------------- menus --

enum class MenuItemType : std::uint32_t {
  kCommand = 0,
  kCheck = 1,
  kRadio = 2,
  kSeparator = 3,
  kSubmenu = 4,
  kTitle = 5,
  kHeader = 6,
  kControls = 7,
  kGrid = 8,
  kSection = 9,
  kFooter = 10,
};

struct MenuItem {
  std::int32_t command_id = 0;
  MenuItemType type = MenuItemType::kCommand;
  bool enabled = true;
  bool checked = false;
  bool keep_open = false;
  std::string label;
  std::string accelerator;
  std::string access_key;  // UTF-8 mnemonic from the host label; may be empty
  ImageId icon;
  std::int32_t submenu = -1;  // index into MenuTree::menus
  std::string glyph;
  std::optional<MenuItemMetadata> metadata;
  std::string badge;
  Color badge_color;

  [[nodiscard]] bool IsVisible() const noexcept {
    return !metadata || metadata->flags.Has(MenuMetadataFlag::kVisible);
  }
};

// menus[0] is the root; an item's `submenu` indexes into `menus`.
struct MenuTree {
  std::vector<std::vector<MenuItem>> menus;
  bool panel = false;
};

// --------------------------------------------------------------- intents --

enum class AddressKey : std::uint32_t {
  kUp,
  kDown,
  kPageUp,
  kPageDown,
  kTab,
  kEscape,
  kDeleteMatch,
  kControl
};
enum class AddressModifier : std::uint32_t {
  kShift = 1u << 0,
  kControl = 1u << 1
};
template <>
inline constexpr bool kIsFlagEnum<AddressModifier> = true;
enum class NavCommand : std::uint32_t {
  kBack = 0,
  kForward = 1,
  kReload = 2,
  kStop = 3,
  kHome = 4,
};

enum class Disposition : std::uint32_t {
  kCurrentTab = 0,
  kNewTab = 1,
  kNewBackgroundTab = 2,
  kNewWindow = 3,
};

enum class WindowCommand : std::uint32_t {
  kMinimize = 0,
  kToggleMaximize = 1,
  kClose = 2,
};

enum class MenuKind : std::uint32_t {
  kApp = 0,
  kTab = 1,
  kBackHistory = 2,
  kForwardHistory = 3,
  kReload = 4,
  kAction = 5,
  kSidebar = 6,
  kSpace = 7,
  kSettings = 8,
  kTheme = 9,
  kCreate = 10,
  kPage = 11,
  kWorkspaces = 12,
  kLibrary = 13,
  kSiteData = 14,
  kSidebarLayout = 15,
  kSecurity = 16,
  kDownload = 17,     // target: DownloadId
  kLibraryItem = 18,  // target: LibraryItemId
};

// The sidebar's two toggle buttons. The one in the sidebar's top row steps
// through every presentation; the footer one only expands and collapses.
enum class SidebarToggle { kCycle, kCollapse, kCompact };

enum class AnchorKind : std::uint32_t {
  kElement = 0,
  kAction = 1,
  kTab = 2,
};

enum class Element : std::uint32_t {
  kAddress = 1,
  kSecurityChip = 2,
  kBack = 3,
  kForward = 4,
  kReload = 5,
  kNewTab = 6,
  kAppMenu = 7,
  kSidebar = 8,
  kToolbar = 9,
  kSidebarToggle = 10,
  kLibrary = 11,
  kCreate = 12,
  kWorkspace = 13,
  kCompactMode = 14,
};

// Where a native element is, in window client DIPs, so the host can anchor
// its own popups to it.
struct Anchor {
  AnchorKind kind = AnchorKind::kElement;
  std::uint64_t id = 0;
  Rect bounds;
  bool visible = true;

  friend bool operator==(const Anchor&, const Anchor&) = default;
};

}  // namespace kr
