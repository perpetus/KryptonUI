#include "chrome_commands.hpp"

#include <algorithm>
#include <utility>

#include "krypton/fixtures/mock_browser.hpp"

namespace kr::fixtures {
bool MockBrowser::RunChromeCommand(std::int32_t id) {
  using C = ChromeCommand;
  const auto switch_index = id - std::to_underlying(C::kSwitchWorkspaceBase);
  if (switch_index >= 0 &&
      static_cast<std::size_t>(switch_index) < spaces_.size()) {
    SwitchSpace(spaces_[switch_index].id);
    return true;
  }
  const auto page = [&](std::string_view url) {
    OpenTab(url, KR_OPEN_NEW_TAB);
  };
  const auto command = static_cast<C>(id);
  switch (command) {
    case C::kCreateFolder: {
      const KrFolderCommand folder{.struct_size = sizeof(KrFolderCommand),
                                   .kind = KR_FOLDER_CREATE,
                                   .space = active_space_};
      return ExecuteFolderCommand(folder) == KR_OK;
    }
    case C::kCreateSplit:
      if (!options_.multi_page ||
          !KR_HAS_SLOT(&ui_, KrShellUI, show_split_chooser) ||
          !ui_.show_split_chooser)
        return false;
      return ui_.show_split_chooser(ui_.self) == KR_OK;
    case C::kNewTab:
      OpenTab("", KR_OPEN_NEW_TAB);
      FocusAddress(true);
      break;
    case C::kNewWindow:
      if (callbacks_.new_window)
        callbacks_.new_window();
      break;
    case C::kReopen:
      ReopenClosedTab();
      break;
    case C::kHistory:
      page("krypton://history");
      break;
    case C::kClearHistory:
      page("krypton://history/clear");
      break;
    case C::kBookmarks:
      page("krypton://bookmarks");
      break;
    case C::kBookmarkPage:
      bookmarked_ = !bookmarked_;
      PushActions();
      break;
    case C::kDownloads:
      page("krypton://downloads");
      break;
    case C::kPasswords:
      page("krypton://passwords");
      break;
    case C::kExtensions:
      page("krypton://extensions");
      break;
    case C::kGetExtensions:
      page("krypton://extensions/discover");
      break;
    case C::kSettings:
      OpenSettings();
      break;
    case C::kTaskManager:
      ShowTaskManager();
      break;
    case C::kHelp:
      page("krypton://help");
      break;
    case C::kShortcuts:
      page("krypton://help/keyboard-shortcuts");
      break;
    case C::kTroubleshoot:
      page("krypton://support");
      break;
    case C::kAbout:
      page("krypton://about");
      break;
    case C::kManageWorkspaces:
      if (KR_HAS_SLOT(&ui_, KrShellUI, show_workspace_editor) &&
          ui_.show_workspace_editor)
        ShowWorkspaceEditor(chrome_menu_target_ ? chrome_menu_target_
                                                : active_space_);
      else
        page("krypton://settings/workspaces");
      break;
    case C::kCreateWorkspace:
      ShowWorkspaceEditor();
      break;
    case C::kRenameWorkspace:
    case C::kWorkspaceIcon:
      ShowWorkspaceEditor(chrome_menu_target_ ? chrome_menu_target_
                                              : active_space_);
      break;
    case C::kDeleteWorkspace: {
      const KrSpace target{
          .struct_size = sizeof(KrSpace),
          .id = chrome_menu_target_ ? chrome_menu_target_ : active_space_};
      EditWorkspace(KR_WORKSPACE_REMOVE, target);
      break;
    }
    case C::kWorkspaceRouting:
      page("krypton://settings/workspace-routing");
      break;
    case C::kWorkspaceTheme:
      ShowThemeEditor(chrome_menu_target_ ? chrome_menu_target_
                                          : active_space_);
      break;
    case C::kMedia:
      page("krypton://library/media");
      break;
    case C::kBoosts:
      page("krypton://mods");
      break;
    case C::kSiteSecurity:
      page("krypton://site-information");
      break;
    case C::kTrackingProtection:
      page("krypton://settings/tracking-protection");
      break;
    case C::kExtensionBlocker:
      page("krypton://extensions/ublock-origin");
      break;
    case C::kSiteSettings:
      ShowSitePermissions(ActiveOrigin());
      break;
    case C::kFullscreen:
      ToggleFullscreen();
      break;
    case C::kCompact:
      ToggleCompactMode();
      break;
    case C::kHideSidebar:
    case C::kHideToolbar:
    case C::kHideBoth:
      options_.appearance.layout_flags &=
          ~(KR_LAYOUT_COMPACT_HIDE_SIDEBAR | KR_LAYOUT_COMPACT_HIDE_TOOLBAR);
      options_.appearance.layout_flags |=
          KR_LAYOUT_COMPACT_MODE | KR_LAYOUT_COMPACT_TARGETS_EXPLICIT;
      if (static_cast<C>(id) != C::kHideToolbar)
        options_.appearance.layout_flags |= KR_LAYOUT_COMPACT_HIDE_SIDEBAR;
      if (static_cast<C>(id) != C::kHideSidebar)
        options_.appearance.layout_flags |= KR_LAYOUT_COMPACT_HIDE_TOOLBAR;
      AppearanceChanged();
      break;
    case C::kLayoutClassic:
    case C::kLayoutCompact:
    case C::kLayoutDynamic:
      options_.appearance.layout_flags &=
          ~(KR_LAYOUT_TABS_CLASSIC | KR_LAYOUT_TABS_COMPACT |
            KR_LAYOUT_TABS_DYNAMIC);
      options_.appearance.layout_flags |=
          command == C::kLayoutClassic   ? KR_LAYOUT_TABS_CLASSIC
          : command == C::kLayoutCompact ? KR_LAYOUT_TABS_COMPACT
                                         : KR_LAYOUT_TABS_DYNAMIC;
      AppearanceChanged();
      break;
    case C::kCenteredAddress:
    case C::kMinimalAddress:
    case C::kRoundedFrame:
      options_.appearance.layout_flags ^=
          command == C::kCenteredAddress  ? KR_LAYOUT_CENTERED_ADDRESS
          : command == C::kMinimalAddress ? KR_LAYOUT_MINIMAL_ADDRESS
                                          : KR_LAYOUT_SQUARE_FRAME;
      AppearanceChanged();
      break;
    case C::kPinTop:
    case C::kPinSide: {
      auto& flags = options_.appearance.layout_flags;
      if (!(flags & KR_LAYOUT_COMPACT_TARGETS_EXPLICIT) &&
          !(flags &
            (KR_LAYOUT_COMPACT_HIDE_SIDEBAR | KR_LAYOUT_COMPACT_HIDE_TOOLBAR)))
        flags |= (flags & (KR_LAYOUT_TABS_CLASSIC | KR_LAYOUT_TABS_COMPACT |
                           KR_LAYOUT_TABS_DYNAMIC))
                     ? KR_LAYOUT_COMPACT_HIDE_TOOLBAR
                     : KR_LAYOUT_COMPACT_HIDE_SIDEBAR;
      flags |= KR_LAYOUT_COMPACT_TARGETS_EXPLICIT;
      flags ^= command == C::kPinTop ? KR_LAYOUT_COMPACT_HIDE_TOOLBAR
                                     : KR_LAYOUT_COMPACT_HIDE_SIDEBAR;
      AppearanceChanged();
      break;
    }
    case C::kSidebarRight:
      options_.appearance.layout_flags ^= KR_LAYOUT_SIDEBAR_RIGHT;
      AppearanceChanged();
      break;
    case C::kLayoutSingle:
    case C::kLayoutMultiple:
    case C::kLayoutCollapsed:
      options_.appearance.layout_flags &=
          ~(KR_LAYOUT_TABS_CLASSIC | KR_LAYOUT_TABS_COMPACT |
            KR_LAYOUT_TABS_DYNAMIC | KR_LAYOUT_TOOLBAR_SINGLE |
            KR_LAYOUT_TOOLBAR_COLLAPSED | KR_LAYOUT_SIDEBAR_ICON_ONLY |
            KR_LAYOUT_SIDEBAR_AUTOHIDE | KR_LAYOUT_SIDEBAR_COLLAPSED);
      if (static_cast<C>(id) == C::kLayoutSingle)
        options_.appearance.layout_flags |= KR_LAYOUT_TOOLBAR_SINGLE;
      if (static_cast<C>(id) == C::kLayoutCollapsed)
        options_.appearance.layout_flags |=
            KR_LAYOUT_TOOLBAR_COLLAPSED | KR_LAYOUT_SIDEBAR_ICON_ONLY;
      AppearanceChanged();
      break;
    case C::kZoomOut:
    case C::kZoomReset:
    case C::kZoomIn:
      // Sample page zoom belongs to the fake host, never to shell layout.
      page_zoom_percent_ =
          static_cast<C>(id) == C::kZoomReset
              ? 100
              : std::clamp(page_zoom_percent_ +
                               (static_cast<C>(id) == C::kZoomIn ? 10 : -10),
                           30, 500);
      if (callbacks_.page_changed)
        callbacks_.page_changed();
      if (chrome_menu_request_)
        (void)ShowChromeMenu(chrome_menu_request_, chrome_menu_kind_,
                             chrome_menu_target_);
      break;
    case C::kExit:
      if (callbacks_.window_command)
        callbacks_.window_command(KR_WINDOW_CLOSE);
      break;
    default:
      return false;
  }
  return true;
}
}  // namespace kr::fixtures
