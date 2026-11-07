// Menu structure follows the supplied app-menu image and the checked-in
// zen-panels/popups.inc, site-data.inc and ZenLibrary.mjs sections.
#include <algorithm>
#include <array>
#include <format>
#include <utility>

#include "chrome_commands.hpp"
#include "krypton/fixtures/mock_browser.hpp"
#include "krypton/ui/design/tokens.hpp"

namespace kr::fixtures {
namespace {
KrStr Text(std::string_view value) {
  return {value.data(), value.size()};
}
KrMenuItem Item(ChromeCommand command,
                std::string_view label,
                std::string_view glyph = {},
                std::string_view shortcut = {},
                bool enabled = true) {
  return {.struct_size = sizeof(KrMenuItem),
          .command_id = std::to_underlying(command),
          .type = KR_MENU_ITEM_COMMAND,
          .flags = enabled ? KR_MENU_ITEM_ENABLED : 0u,
          .label = Text(label),
          .accelerator = Text(shortcut),
          .submenu = -1,
          .glyph = Text(glyph)};
}
KrMenuItem Branch(std::string_view label,
                  std::size_t child,
                  std::string_view glyph = {},
                  std::uint32_t type = KR_MENU_ITEM_SUBMENU) {
  auto value = Item(ChromeCommand::kSignIn, label, glyph);
  value.command_id = 0;
  value.type = type;
  value.submenu = static_cast<std::int32_t>(child);
  return value;
}
constexpr KrMenuItem kSeparator{.struct_size = sizeof(KrMenuItem),
                                .type = KR_MENU_ITEM_SEPARATOR,
                                .submenu = -1};
}  // namespace

std::string_view MockBrowser::LayoutMenuText(std::string_view key,
                                             std::string_view fallback) const {
  for (const auto& [name, value] : layout_text_)
    if (name == key)
      return value;
  return fallback;
}

bool MockBrowser::ShowChromeMenu(std::uint32_t request,
                                 std::uint32_t kind,
                                 std::uint64_t target) {
  if (!attached_ ||
      (kind != KR_MENU_APP && kind != KR_MENU_CREATE && kind != KR_MENU_SPACE &&
       kind != KR_MENU_WORKSPACES && kind != KR_MENU_LIBRARY &&
       kind != KR_MENU_SITE_DATA && kind != KR_MENU_SIDEBAR_LAYOUT &&
       kind != KR_MENU_SECURITY))
    return false;
  using C = ChromeCommand;
  const bool workspace_editable =
      KR_HAS_SLOT(&ui_, KrShellUI, show_workspace_editor) &&
      ui_.show_workspace_editor;
  const auto workspace_target = target ? target : active_space_;
  const bool workspace_removable = workspace_editable && !spaces_.empty() &&
                                   workspace_target != spaces_.front().id;
  enum Menu : std::size_t {
    kRoot,
    kAccount,
    kHistory,
    kClosedTabs,
    kBookmarks,
    kZoom,
    kTools,
    kHelp,
    kLayouts,
    kHorizontalLayouts,
    kVerticalLayouts,
    kCompact,
    kSpaces,
    kContainers,
    kSitePageActions,
    kSiteManage,
    kSiteExtensions,
    kSiteSettings,
    kSiteFooter,
    kSiteMore,
    kCount
  };
  std::array<std::vector<KrMenuItem>, kCount> menus;
  const bool tiles = kind == KR_MENU_SITE_DATA &&
                     KR_HAS_SLOT(&ui_, KrShellUI, show_menu_panel_with_tiles) &&
                     ui_.show_menu_panel_with_tiles;
  const bool panel = tiles || (kind == KR_MENU_APP &&
                               KR_HAS_SLOT(&ui_, KrShellUI, show_menu_panel) &&
                               ui_.show_menu_panel);
  const bool single =
      (options_.appearance.layout_flags & KR_LAYOUT_TOOLBAR_SINGLE) != 0;
  const bool collapsed =
      (options_.appearance.layout_flags &
       (KR_LAYOUT_TOOLBAR_COLLAPSED | KR_LAYOUT_SIDEBAR_ICON_ONLY)) != 0;
  const auto check = [](KrMenuItem value, bool checked) {
    value.type = KR_MENU_ITEM_CHECK;
    if (checked)
      value.flags |= KR_MENU_ITEM_CHECKED;
    return value;
  };
  menus[kAccount] = {Item(C::kSignIn, "Sign In", {}, {}, false)};
  menus[kClosedTabs] = {Item(C::kReopen, "Reopen Closed Tab", "\uE7A7",
                             "Ctrl+Shift+T", !closed_tabs_.empty())};
  menus[kHistory] = {Item(C::kHistory, "Manage History", "\uE81C", "Ctrl+H"),
                     Item(C::kClearHistory, "Clear Recent History...", "\uE74D",
                          "Ctrl+Shift+Delete"),
                     kSeparator,
                     Branch("Recently Closed Tabs", kClosedTabs, "\uE8F1")};
  menus[kBookmarks] = {
      Item(C::kBookmarkPage, "Bookmark Current Tab", "\uE734", "Ctrl+D"),
      Item(C::kBookmarks, "Manage Bookmarks", "\uE8A4", "Ctrl+Shift+O"),
      Item(C::kBookmarkToolbar, "Bookmarks Toolbar", "\uE8A4", {}, false)};
  constexpr auto horizontal_mask =
      KR_LAYOUT_TABS_CLASSIC | KR_LAYOUT_TABS_COMPACT | KR_LAYOUT_TABS_DYNAMIC;
  const auto flags = options_.appearance.layout_flags;
  const bool vertical = (flags & horizontal_mask) == 0;
  const auto label = [this](std::string_view key, std::string_view fallback) {
    return LayoutMenuText(key, fallback);
  };
  const auto radio = [&](KrMenuItem value, bool selected) {
    value = check(value, selected);
    value.type = KR_MENU_ITEM_RADIO;
    return value;
  };
  menus[kHorizontalLayouts] = {
      radio(Item(C::kLayoutClassic, label("layout.classic", "Classic")),
            (flags & KR_LAYOUT_TABS_CLASSIC) != 0),
      radio(Item(C::kLayoutCompact, label("layout.compact", "Compact")),
            (flags & KR_LAYOUT_TABS_COMPACT) != 0),
      radio(Item(C::kLayoutDynamic, label("layout.dynamic", "Dynamic")),
            (flags & KR_LAYOUT_TABS_DYNAMIC) != 0)};
  menus[kVerticalLayouts] = {
      radio(Item(C::kLayoutSingle, "Only Sidebar"),
            vertical && single && !collapsed),
      radio(Item(C::kLayoutMultiple, "Sidebar and Top Toolbar"),
            vertical && !single && !collapsed),
      radio(Item(C::kLayoutCollapsed, "Collapsed Sidebar"),
            vertical && collapsed),
      kSeparator,
      check(Item(C::kSidebarRight, "Sidebar on right"),
            (flags & KR_LAYOUT_SIDEBAR_RIGHT) != 0)};
  menus[kLayouts] = {
      Branch(label("layout.horizontal_tabs", "Horizontal Tabs"),
             kHorizontalLayouts),
      Branch(label("layout.vertical_tabs", "Vertical Tabs"), kVerticalLayouts),
      kSeparator,
      check(
          Item(C::kCenteredAddress,
               label("layout.centered_address", "Centered Address Bar"), {}, {},
               (flags & KR_LAYOUT_TABS_COMPACT) == 0 &&
                   !(vertical && single && !collapsed)),
          (flags & KR_LAYOUT_CENTERED_ADDRESS) != 0),
      check(Item(C::kMinimalAddress,
                 label("layout.minimal_address", "Minimal Address Bar")),
            (flags & KR_LAYOUT_MINIMAL_ADDRESS) != 0),
      check(Item(C::kRoundedFrame,
                 label("layout.rounded_frame", "Rounded Frame")),
            (flags & KR_LAYOUT_SQUARE_FRAME) == 0)};
  const bool explicit_targets =
      (flags & KR_LAYOUT_COMPACT_TARGETS_EXPLICIT) != 0;
  const bool hide_top = (flags & KR_LAYOUT_COMPACT_HIDE_TOOLBAR) != 0 ||
                        (!vertical && !explicit_targets &&
                         (flags & KR_LAYOUT_COMPACT_HIDE_SIDEBAR) == 0);
  const bool hide_side = (flags & KR_LAYOUT_COMPACT_HIDE_SIDEBAR) != 0 ||
                         (vertical && !explicit_targets &&
                          (flags & KR_LAYOUT_COMPACT_HIDE_TOOLBAR) == 0);
  menus[kCompact] = {
      check(Item(C::kCompact, "Enable Compact Mode", "\uE8A7", "Ctrl+Alt+C"),
            (options_.appearance.layout_flags & KR_LAYOUT_COMPACT_MODE) != 0),
      kSeparator,
      check(Item(C::kHideSidebar, "Hide Tab bar", {}, {}, vertical),
            hide_side && !hide_top),
      check(Item(C::kHideToolbar, "Hide Top bar"), hide_top && !hide_side),
      check(Item(C::kHideBoth, "Hide Both", {}, {}, vertical),
            hide_side && hide_top),
      Branch("Browser Layout", kLayouts, "\uE8A7")};
  menus[kCompact].push_back(kSeparator);
  menus[kCompact].push_back(
      check(Item(C::kPinTop, label("layout.pin_top", "Pin Top Chrome"), {}, {},
                 !(vertical && single && !collapsed)),
            !hide_top));
  menus[kCompact].push_back(
      check(Item(C::kPinSide, label("layout.pin_side", "Pin Side Chrome"), {},
                 {}, vertical),
            !hide_side));

  menus[kTools] = {
      Item(C::kCustomize, "Customize Toolbar...", "\uE713", {}, false),
      Branch("Browser Layout", kLayouts, "\uE8A7"),
      Branch("Compact Mode", kCompact, "\uE8A7"),
      kSeparator,
      Item(C::kWorkspaceTheme, "Change Theme Colors", "\uE790", {},
           KR_HAS_SLOT(&ui_, KrShellUI, show_theme_editor) &&
               ui_.show_theme_editor),
      Item(C::kDeveloperTools, "Web Developer Tools", "\uE943", "Ctrl+Shift+I",
           false),
      Item(C::kTaskManager, "Task Manager", "\uE9D9", "Shift+Esc"),
      Item(C::kScreenshot, "Take Screenshot", "\uE722", "Ctrl+Shift+S", false),
      Item(C::kViewSource, "Page Source", "\uE943", "Ctrl+U", false)};
  if (!vertical)
    menus[kTools].push_back(Branch("Workspaces", kSpaces, "\uE774"));
  menus[kHelp] = {
      Item(C::kHelp, "Help", "\uE897"),
      Item(C::kReport, "Report a Problem...", "\uE90A", {}, false),
      Item(C::kShortcuts, "Keyboard Shortcuts", "\uE765"),
      Item(C::kTroubleshoot, "Troubleshooting Information", "\uE946"),
      kSeparator,
      Item(C::kAbout, "About Krypton", "\uE946")};
  for (std::size_t i = 0; i < spaces_.size(); ++i) {
    auto entry =
        Item(static_cast<C>(std::to_underlying(C::kSwitchWorkspaceBase) +
                            static_cast<std::int32_t>(i)),
             spaces_[i].name, "\uE774");
    entry.type = KR_MENU_ITEM_RADIO;
    if (spaces_[i].id == active_space_)
      entry.flags |= KR_MENU_ITEM_CHECKED;
    menus[kSpaces].push_back(entry);
  }
  const auto percent = std::format("{}%", page_zoom_percent_);
  menus[kZoom] = {
      Item(C::kZoomOut, "Zoom out", "\uE738", "Ctrl+-",
           page_zoom_percent_ > 30),
      Item(C::kZoomReset, percent, {}, "Ctrl+0"),
      Item(C::kZoomIn, "Zoom in", "\uE710", "Ctrl++", page_zoom_percent_ < 500),
      kSeparator, Item(C::kFullscreen, "Full screen", "\uE740", "F11")};
  for (auto& item : menus[kZoom])
    if (item.command_id == std::to_underlying(C::kZoomOut) ||
        item.command_id == std::to_underlying(C::kZoomReset) ||
        item.command_id == std::to_underlying(C::kZoomIn))
      item.flags |= KR_MENU_ITEM_KEEP_OPEN;
  if (kind == KR_MENU_APP) {
    menus[kRoot] = {
        Branch("Sync and Save Data", kAccount, {}, KR_MENU_ITEM_HEADER),
        kSeparator,
        Item(C::kNewTab, "New Tab", "\uE8A5", "Ctrl+T"),
        Item(C::kNewWindow, "New Window", "\uE78B", "Ctrl+N"),
        Item(C::kNewBlankWindow, "New Blank Window", "\uE78B", "Ctrl+Shift+N",
             false),
        Item(C::kNewPrivateWindow, "New Private Window", "\uE890",
             "Ctrl+Shift+P", false),
        kSeparator,
        Branch("History", kHistory, "\uE81C"),
        Branch("Bookmarks", kBookmarks, "\uE8A4"),
        Item(C::kDownloads, "Downloads", "\uE896", "Ctrl+J"),
        Item(C::kPasswords, "Passwords", "\uE8D7"),
        Item(C::kExtensions, "Extensions and Themes", "\uEA86", "Ctrl+Shift+A"),
        kSeparator,
        Item(C::kPrint, "Print...", "\uE749", "Ctrl+P", false),
        Item(C::kSavePage, "Save Page As...", "\uE74E", "Ctrl+S", false),
        Item(C::kTranslate, "Translate Page...", "\uF2B7", {}, false),
        Item(C::kFind, "Find in Page...", "\uE721", "Ctrl+F", false),
        kSeparator,
        Branch("Zoom", kZoom, {}, KR_MENU_ITEM_CONTROLS),
        kSeparator,
        Item(C::kSettings, "Settings", "\uE713"),
        Branch("More Tools", kTools, "\uE712"),
        Branch("Help and Report", kHelp, "\uE897"),
        kSeparator,
        Item(C::kExit, "Exit", "\uE8BB", "Ctrl+Shift+Q")};
  } else if (kind == KR_MENU_CREATE) {
    menus[kRoot] = {Item(C::kCreateWorkspace, "New Workspace", "\uE710", {},
                         workspace_editable),
                    Item(C::kCreateFolder, "New Folder", "\uE8F4"), kSeparator,
                    Item(C::kCreateSplit, "New Split View", "\uE89A", {},
                         options_.multi_page &&
                             KR_HAS_SLOT(&ui_, KrShellUI, show_split_chooser) &&
                             ui_.show_split_chooser),
                    Item(C::kNewTab, "New Tab", "\uE8A5", "Ctrl+T")};
  } else if (kind == KR_MENU_WORKSPACES) {
    menus[kRoot] = menus[kSpaces];
    menus[kRoot].push_back(kSeparator);
    menus[kRoot].push_back(Item(C::kCreateWorkspace, "New Workspace", "\uE710",
                                {}, workspace_editable));
    menus[kRoot].push_back(
        Item(C::kManageWorkspaces, "Manage Workspaces", "\uE713"));
  } else if (kind == KR_MENU_SPACE) {
    menus[kContainers] = {Item(C::kManageWorkspaces, "Default", {}, {}, false)};
    menus[kRoot] = {
        Item(C::kRenameWorkspace, "Change Name...", "\uE8AC", {},
             workspace_editable),
        Item(C::kWorkspaceIcon, "Change Icon...", "\uE8B9", {},
             workspace_editable),
        Item(C::kWorkspaceTheme, "Change Theme Colors", "\uE790", {},
             KR_HAS_SLOT(&ui_, KrShellUI, show_theme_editor) &&
                 ui_.show_theme_editor),
        Branch("Default Container", kContainers),
        kSeparator,
        Item(C::kManageWorkspaces, "Manage Workspaces", "\uE713"),
        Item(C::kUnloadWorkspace, "Unload Workspace", "\uE74D", {}, false),
        Item(C::kUnloadOtherWorkspaces, "Unload All Other Workspaces", "\uE74D",
             {}, false),
        kSeparator,
        Item(C::kCreateWorkspace, "New Workspace", "\uE710", {},
             workspace_editable),
        Item(C::kShareWorkspace, "Share Workspace", "\uE72D", {}, false),
        Item(C::kDeleteWorkspace, "Delete Workspace", "\uE74D", {},
             workspace_removable),
        kSeparator,
        Item(C::kWorkspaceRouting, "Workspace Routing Settings", "\uE713")};
  } else if (kind == KR_MENU_LIBRARY) {
    menus[kRoot] = {Item(C::kMedia, "Media", "\uE8B9"),
                    Item(C::kDownloads, "Downloads", "\uE896", "Ctrl+J"),
                    Item(C::kBoosts, "Mods", "\uE790"),
                    Branch("Workspaces", kSpaces, "\uE774"),
                    Branch("History", kHistory, "\uE81C"),
                    Branch("Bookmarks", kBookmarks, "\uE8A4")};
  } else if (kind == KR_MENU_SITE_DATA) {
    menus[kRoot] = {
        Item(C::kSharePage, "Share Page", "\uE72D", {}, false),
        Item(C::kReader, "Reader View", "\uE8A4", {}, false),
        Item(C::kScreenshot, "Take Screenshot", "\uE722", {}, false),
        check(
            Item(C::kBookmarkPage, "Bookmark Current Tab", "\uE734", "Ctrl+D"),
            bookmarked_),
        kSeparator,
        Item(C::kExtensions, "Manage Extensions", "\uEA86", "Ctrl+Shift+A"),
        Item(C::kGetExtensions, "Get Extensions", "\uE710"),
        Item(C::kBoosts, "Site Mods", "\uE790"),
        kSeparator,
        Item(C::kSiteSecurity, "Site Information", "\uE72E"),
        Item(C::kSiteSettings, "Site Settings", "\uE713"),
        Item(C::kClearSiteData, "Clear Site Data...", "\uE74D", {}, false)};
  } else if (kind == KR_MENU_SECURITY) {
    menus[kRoot] = {
        Item(C::kSiteSecurity, "Connection Information", "\uE72E"),
        Item(C::kSiteSettings, "Permissions for This Site", "\uE713"),
        Item(C::kClearSiteData, "Clear Cookies and Site Data...", "\uE74D", {},
             false)};
  } else {
    menus[kRoot] = menus[kCompact];
  }
  chrome_menu_request_ = request;
  chrome_menu_kind_ = kind;
  chrome_menu_target_ = target;
  // Sized metadata and strings are borrowed only for this synchronous delivery.
  KrMenuItemMetadata protection{}, manage_metadata{};
  if (tiles) {
    menus[kSitePageActions] = {
        Item(C::kSharePage, "Share Page", "\uE72D", {}, false),
        Item(C::kReader, "Reader View", "\uE8A4", {}, false),
        Item(C::kScreenshot, "Take Screenshot", "\uE722", {}, false),
        check(Item(C::kBookmarkPage, "Bookmark Current Tab",
                   bookmarked_ ? "\uE735" : "\uE734", "Ctrl+D"),
              bookmarked_)};
    if (single && !(flags & KR_LAYOUT_SIDEBAR_RIGHT))
      std::ranges::reverse(menus[kSitePageActions]);
    // The preview has one installed extension. A browser supplies its own
    // installed extension commands, images, enabled states and badges.
    auto blocker = Item(C::kExtensionBlocker, "uBlock Origin", "\uEA18");
    blocker.badge = Text("12");
    blocker.badge_color = design::kTokens.site_data_menu.badge_background;
    menus[kSiteExtensions] = {
        blocker, Item(C::kGetExtensions, "Get Extensions", "\uE710")};
    manage_metadata.struct_size = sizeof(manage_metadata);
    manage_metadata.flags = KR_MENU_META_VISIBLE;
    manage_metadata.accessible_name = Text("Manage Extensions");
    auto manage = Item(C::kExtensions, "Manage");
    manage.metadata = &manage_metadata;
    menus[kSiteManage] = {manage};
    menus[kSiteMore] = {
        Item(C::kSiteSecurity, "Site Information", "\uE72E"),
        Item(C::kSiteSettings, "Site Settings", "\uE713"),
        Item(C::kClearSiteData, "Clear Site Data...", "\uE74D", {}, false)};
    const auto* site = Find(active_tab_);
    const bool secure = site && site->url.starts_with("https://");
    menus[kSiteFooter] = {
        Item(C::kSiteSecurity, secure ? "Secure" : "Site Info",
             secure ? "\uE72E" : "\uE946"),
        Item(C::kBoosts, "Site Mods", "\uE790"),
        Branch("More", kSiteMore, "\uE712")};
    protection.struct_size = sizeof(protection);
    protection.flags = KR_MENU_META_VISIBLE;
    protection.secondary_label = Text("Enabled");
    auto tracking =
        Item(C::kTrackingProtection, "Tracking Protection", "\uEA18");
    tracking.metadata = &protection;
    menus[kRoot] = {
        Branch("Page Actions", kSitePageActions, {}, KR_MENU_ITEM_GRID),
        Branch("Extensions", kSiteManage, {}, KR_MENU_ITEM_SECTION),
        Branch("Installed Extensions", kSiteExtensions, {}, KR_MENU_ITEM_GRID),
        Branch("Settings", kSiteSettings, {}, KR_MENU_ITEM_SECTION),
        tracking,
        Branch("Site Controls", kSiteFooter, {}, KR_MENU_ITEM_FOOTER)};
  }
  if (!panel)
    for (auto& menu : menus)
      for (auto& item : menu)
        if (item.type == KR_MENU_ITEM_HEADER ||
            item.type == KR_MENU_ITEM_CONTROLS)
          item.type = KR_MENU_ITEM_SUBMENU;
  std::array<KrMenu, kCount> rich;
  for (std::size_t i = 0; i < menus.size(); ++i)
    rich[i] = {sizeof(KrMenu), menus[i].data(), menus[i].size(),
               sizeof(KrMenuItem)};
  if (tiles)
    ui_.show_menu_panel_with_tiles(ui_.self, request, rich.data(), rich.size(),
                                   sizeof(KrMenu));
  else if (panel)
    ui_.show_menu_panel(ui_.self, request, rich.data(), rich.size(),
                        sizeof(KrMenu));
  else if (KR_HAS_SLOT(&ui_, KrShellUI, show_menu_with_glyphs) &&
           ui_.show_menu_with_glyphs)
    ui_.show_menu_with_glyphs(ui_.self, request, rich.data(), rich.size(),
                              sizeof(KrMenu));
  else
    ui_.show_menu(ui_.self, request, rich.data(), rich.size(), sizeof(KrMenu));
  return true;
}
}  // namespace kr::fixtures
