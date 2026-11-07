#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "krypton/fixtures/mock_browser.hpp"
#include "krypton/testing/test.hpp"

namespace kr::fixtures {
namespace {
struct Row {
  std::int32_t command;
  std::uint32_t type, flags;
  std::string label, glyph;
  std::int32_t child;
  std::string badge, secondary;
};
struct Recorder {
  std::vector<std::vector<Row>> menus;
  std::vector<std::uint32_t> action_kinds;
  std::vector<KrActionId> action_ids;
  std::string latest_url;
  std::vector<KrLoadProgress> load_progress;
  bool loading = false;
  KrSpaceId space = 0, editor = 0;
  std::uint32_t request = 0, context = 0;
  bool panel = false;
  KrSpaceId workspace_editor = 0;
  int workspace_opens = 0;
  void Menu(std::uint32_t id,
            const KrMenu* input,
            std::size_t count,
            bool rich) {
    request = id;
    panel = rich;
    menus.clear();
    menus.resize(count);
    for (std::size_t m = 0; m < count; ++m)
      for (std::size_t i = 0; i < input[m].count; ++i) {
        const auto& wire = input[m].items[i];
        const auto& item = wire;
        KR_EXPECT_EQ(input[m].struct_size, sizeof(KrMenu));
        KR_EXPECT_EQ(input[m].item_size, sizeof(KrMenuItem));
        KR_EXPECT_EQ(item.struct_size, sizeof(KrMenuItem));
        menus[m].push_back(
            {item.command_id, item.type, item.flags,
             std::string(item.label.data ? item.label.data : "",
                         item.label.size),
             std::string(wire.glyph.data ? wire.glyph.data : "",
                         wire.glyph.size),
             item.submenu,
             std::string(item.badge.data ? item.badge.data : "",
                         item.badge.size),
             item.metadata
                 ? std::string(item.metadata->secondary_label.data
                                   ? item.metadata->secondary_label.data
                                   : "",
                               item.metadata->secondary_label.size)
                 : std::string{}});
      }
  }
  const Row& Find(std::string_view label) const {
    for (const auto& menu : menus)
      for (const auto& row : menu)
        if (row.label == label)
          return row;
    KR_ASSERT(false);
    return menus.front().front();
  }
};
Recorder& From(void* self) {
  return *static_cast<Recorder*>(self);
}
KrShellUI Ui(Recorder& recorder, bool panel) {
  KrShellUI ui{};
  ui.struct_size = panel ? sizeof(ui) : offsetof(KrShellUI, show_menu_panel);
  ui.self = &recorder;
  ui.set_theme = [](void*, const KrTheme*) {};
  ui.set_layout_prefs = [](void*, const KrLayoutPrefs*) {};
  ui.put_image = [](void*, const KrImage*) {};
  ui.set_spaces = [](void* self, const KrSpace*, std::size_t, std::size_t,
                     KrSpaceId space) { From(self).space = space; };
  ui.tab_inserted = [](void* self, std::size_t, const KrTab* tab) {
    From(self).latest_url.assign(tab->url.data, tab->url.size);
  };
  ui.tab_updated = [](void*, const KrTab*) {};
  ui.set_active_tab = [](void*, KrTabId) {};
  ui.set_nav_state = [](void* self, const KrNavState* nav) {
    From(self).loading = nav->is_loading;
  };
  ui.set_actions = [](void* self, const KrAction* actions, std::size_t count,
                      std::size_t) {
    auto& r = From(self);
    r.action_ids.clear();
    r.action_kinds.clear();
    for (std::size_t i = 0; i < count; ++i) {
      r.action_ids.push_back(actions[i].id);
      r.action_kinds.push_back(actions[i].kind);
    }
  };
  ui.show_menu_with_glyphs =
      [](void* self, std::uint32_t id, const KrMenu* menus, std::size_t count,
         std::size_t) { From(self).Menu(id, menus, count, false); };
  ui.show_menu_panel = [](void* self, std::uint32_t id, const KrMenu* menus,
                          std::size_t count, std::size_t) {
    From(self).Menu(id, menus, count, true);
  };
  ui.show_theme_editor = [](void* self, KrSpaceId space) {
    From(self).editor = space;
    return KR_OK;
  };
  ui.request_context_menu = [](void* self, std::uint32_t kind, std::uint64_t,
                               KrRect) { From(self).context = kind; };
  ui.focus_address = [](void*, bool) {};
  if (panel)
    ui.show_menu_panel_with_tiles = ui.show_menu_panel;
  ui.set_load_progress = [](void* self, const KrLoadProgress* progress) {
    From(self).load_progress.push_back(*progress);
    return KR_OK;
  };
  return ui;
}
struct Host {
  Recorder record;
  MockBrowser browser{{}, {}};
  explicit Host(bool panel = true,
                std::function<bool(KrTabId, std::uint64_t)> loader = {})
      : browser({}, {.load_requested = std::move(loader)}) {
    browser.Attach(Ui(record, panel));
  }
  ~Host() { browser.Detach(); }
  void Menu(std::uint32_t kind = KR_MENU_APP, std::uint64_t target = 0) {
    const auto& port = browser.table();
    port.request_menu(port.self, 17, kind, target, {});
  }
  void Choose(std::string_view label) {
    const auto row = record.Find(label);
    KR_ASSERT(row.flags & KR_MENU_ITEM_ENABLED);
    const auto& port = browser.table();
    port.menu_command(port.self, record.request, row.command);
  }
};

KR_TEST(FixtureLoadingUsesFiniteHostDrivenStepsAndRejectsSupersededEvents) {
  KrTabId tab = 0;
  std::uint64_t navigation = 0;
  Host host(true, [&](KrTabId id, std::uint64_t sequence) {
    tab = id;
    navigation = sequence;
    return true;
  });
  const auto& port = host.browser.table();
  port.nav_command(port.self, KR_NAV_RELOAD);
  KR_EXPECT(host.record.loading);
  KR_ASSERT(!host.record.load_progress.empty());
  const auto previous = navigation;
  port.nav_command(port.self, KR_NAV_RELOAD);
  KR_EXPECT(navigation > previous);
  KR_EXPECT(!host.browser.AdvanceLoading(tab, previous));
  int steps = 0;
  while (host.browser.AdvanceLoading(tab, navigation) && ++steps < 10) {
  }
  KR_EXPECT(steps < 10);
  KR_EXPECT(!host.record.loading);
  KR_EXPECT_EQ(host.record.load_progress.back().progress, 1.0f);
  KR_EXPECT(
      (host.record.load_progress.back().flags & KR_LOAD_PROGRESS_LOADING) == 0);
  port.nav_command(port.self, KR_NAV_RELOAD);
  port.nav_command(port.self, KR_NAV_STOP);
  KR_EXPECT(!host.browser.AdvanceLoading(tab, navigation));
  port.nav_command(port.self, KR_NAV_RELOAD);
  constexpr std::string_view internal = "krypton://settings";
  port.navigate(port.self, {internal.data(), internal.size()},
                KR_OPEN_CURRENT_TAB);
  KR_EXPECT(!host.record.loading);
  KR_EXPECT(!host.browser.AdvanceLoading(tab, navigation));
}

KR_TEST(ChromeAppPanelIncludesTheReferenceCommandsHeaderAndInlineZoom) {
  Host host;
  host.Menu();
  KR_EXPECT(host.record.panel);
  KR_EXPECT_EQ(host.record.Find("Sync and Save Data").type,
               std::uint32_t{KR_MENU_ITEM_HEADER});
  KR_EXPECT_EQ(host.record.Find("Zoom").type,
               std::uint32_t{KR_MENU_ITEM_CONTROLS});
  for (const auto label :
       {"New Tab", "New Window", "New Blank Window", "New Private Window",
        "History", "Bookmarks", "Downloads", "Passwords",
        "Extensions and Themes", "Print...", "Save Page As...",
        "Translate Page...", "Find in Page...", "Settings", "More Tools",
        "Help and Report", "Exit"})
    (void)host.record.Find(label);
  KR_EXPECT((host.record.Find("New Private Window").flags &
             KR_MENU_ITEM_ENABLED) == 0);
  const auto zoom = host.record.Find("Zoom").child;
  KR_EXPECT_EQ(host.record.menus[zoom].size(), 5u);
  KR_EXPECT(host.record.Find("Zoom in").flags & KR_MENU_ITEM_KEEP_OPEN);
  host.Choose("Zoom in");
  KR_EXPECT(host.browser.page_zoom() > 1);
  KR_EXPECT_EQ(host.record.Find("110%").label, std::string("110%"));
  host.Choose("110%");
  KR_EXPECT_EQ(host.browser.page_zoom(), 1.0f);
  host.Choose("Passwords");
  KR_EXPECT_EQ(host.record.latest_url, std::string("krypton://passwords"));
}

KR_TEST(WorkspaceMenusConnectToTheNativeEditorWhenAvailable) {
  Host host;
  host.Menu(KR_MENU_WORKSPACES);
  KR_EXPECT(!(host.record.Find("New Workspace").flags & KR_MENU_ITEM_ENABLED));
  host.browser.Detach();
  auto ui = Ui(host.record, true);
  ui.show_workspace_editor = [](void* self, KrSpaceId workspace) {
    auto& record = From(self);
    record.workspace_editor = workspace;
    ++record.workspace_opens;
    return KR_OK;
  };
  host.browser.Attach(ui);
  host.Menu(KR_MENU_WORKSPACES);
  host.Choose("New Workspace");
  KR_EXPECT_EQ(host.record.workspace_opens, 1);
  KR_EXPECT_EQ(host.record.workspace_editor, KrSpaceId{0});
  host.Menu(KR_MENU_SPACE, 2);
  host.Choose("Change Name...");
  KR_EXPECT_EQ(host.record.workspace_opens, 2);
  KR_EXPECT_EQ(host.record.workspace_editor, KrSpaceId{2});
  KR_EXPECT(host.record.Find("Delete Workspace").flags & KR_MENU_ITEM_ENABLED);
  host.Menu(KR_MENU_SPACE, 1);
  KR_EXPECT(
      !(host.record.Find("Delete Workspace").flags & KR_MENU_ITEM_ENABLED));
}

KR_TEST(ChromeMenusFallBackToNativeSubmenusForOlderUiTables) {
  Host host(false);
  host.Menu();
  KR_EXPECT(!host.record.panel);
  KR_EXPECT_EQ(host.record.Find("Sync and Save Data").type,
               std::uint32_t{KR_MENU_ITEM_SUBMENU});
  KR_EXPECT_EQ(host.record.Find("Zoom").type,
               std::uint32_t{KR_MENU_ITEM_SUBMENU});
}

KR_TEST(SiteDataPanelUsesSourceSectionsTilesBadgesAndHostCommands) {
  Host host;
  host.Menu(KR_MENU_SITE_DATA);
  KR_EXPECT(host.record.panel);
  const auto& root = host.record.menus.front();
  KR_ASSERT(root.size() == 6);
  KR_EXPECT_EQ(root[0].type, std::uint32_t{KR_MENU_ITEM_GRID});
  KR_EXPECT_EQ(root[1].label, std::string("Extensions"));
  KR_EXPECT_EQ(root[1].type, std::uint32_t{KR_MENU_ITEM_SECTION});
  KR_EXPECT_EQ(root[2].type, std::uint32_t{KR_MENU_ITEM_GRID});
  KR_EXPECT_EQ(root[3].label, std::string("Settings"));
  KR_EXPECT_EQ(root[5].type, std::uint32_t{KR_MENU_ITEM_FOOTER});
  KR_EXPECT_EQ(host.record.menus[root[0].child].size(), 4u);
  KR_EXPECT_EQ(host.record.Find("uBlock Origin").badge, std::string("12"));
  KR_EXPECT_EQ(host.record.Find("Tracking Protection").secondary,
               std::string("Enabled"));
  KR_EXPECT(
      !(host.record.Find("Take Screenshot").flags & KR_MENU_ITEM_ENABLED));
  KR_EXPECT(
      !(host.record.Find("Clear Site Data...").flags & KR_MENU_ITEM_ENABLED));
  host.Choose("Bookmark Current Tab");
  host.Menu(KR_MENU_SITE_DATA);
  const auto& page_actions = host.record.menus[host.record.menus[0][0].child];
  const auto bookmark = std::ranges::find(
      page_actions, std::string("Bookmark Current Tab"), &Row::label);
  KR_ASSERT(bookmark != page_actions.end());
  KR_EXPECT(bookmark->flags & KR_MENU_ITEM_CHECKED);
  host.Choose("Manage");
  KR_EXPECT_EQ(host.record.latest_url, std::string("krypton://extensions"));
  host.Menu(KR_MENU_SITE_DATA);
  host.Choose("Get Extensions");
  KR_EXPECT_EQ(host.record.latest_url,
               std::string("krypton://extensions/discover"));
}

KR_TEST(SiteDataPanelKeepsFlatFallbackWithoutTheTiledCapability) {
  Host host;
  host.browser.Detach();
  auto ui = Ui(host.record, true);
  ui.struct_size = offsetof(KrShellUI, show_menu_panel_with_tiles);
  host.browser.Attach(ui);
  host.Menu(KR_MENU_SITE_DATA);
  KR_EXPECT(!host.record.panel);
  for (const auto& row : host.record.menus.front())
    KR_EXPECT(row.type <= KR_MENU_ITEM_CONTROLS);
  (void)host.record.Find("Manage Extensions");
  (void)host.record.Find("Site Information");
}

KR_TEST(PageContextMenuGroupsNavigationAndBookmarkAboveItsSeparator) {
  Host host;
  host.Menu(KR_MENU_PAGE);
  KR_EXPECT(host.record.panel);
  const auto root = host.record.menus.front();
  KR_ASSERT(root.size() > 1);
  KR_EXPECT_EQ(root.front().type, std::uint32_t{KR_MENU_ITEM_CONTROLS});
  KR_EXPECT(root.front().label.empty());
  KR_EXPECT_EQ(root[1].type, std::uint32_t{KR_MENU_ITEM_SEPARATOR});
  const auto navigation = host.record.menus[root.front().child];
  KR_ASSERT(navigation.size() == 4);
  KR_EXPECT_EQ(navigation[0].label, std::string("Back"));
  KR_EXPECT_EQ(navigation[1].label, std::string("Forward"));
  KR_EXPECT_EQ(navigation[2].label, std::string("Reload"));
  KR_EXPECT_EQ(navigation[3].label, std::string("Bookmark This Page"));
  for (const auto& button : navigation)
    KR_EXPECT(!button.glyph.empty());
  host.Choose("Bookmark This Page");
  host.Menu(KR_MENU_PAGE);
  KR_EXPECT(host.record.Find("Bookmark This Page").flags &
            KR_MENU_ITEM_CHECKED);

  host.browser.Detach();
  auto legacy = Ui(host.record, true);
  legacy.struct_size = offsetof(KrShellUI, show_menu_panel_with_tiles);
  host.browser.Attach(legacy);
  host.Menu(KR_MENU_PAGE);
  KR_EXPECT(!host.record.panel);
  KR_EXPECT_EQ(host.record.menus.front()[0].label, std::string("Back"));
  KR_EXPECT_EQ(host.record.menus.front()[0].flags, navigation[0].flags);
  KR_EXPECT_EQ(host.record.menus.front()[1].label, std::string("Forward"));
}

KR_TEST(ChromeWorkspaceCreateLibraryAndSiteMenusUseSourceEntries) {
  Host host;
  host.Menu(KR_MENU_WORKSPACES);
  host.Choose("Work");
  KR_EXPECT_EQ(host.record.space, 2u);
  host.Menu(KR_MENU_SPACE, 3);
  for (const auto label :
       {"Change Name...", "Change Icon...", "Default Container",
        "Unload Workspace", "Unload All Other Workspaces", "Share Workspace",
        "Delete Workspace", "Workspace Routing Settings"})
    (void)host.record.Find(label);
  host.Choose("Change Theme Colors");
  KR_EXPECT_EQ(host.record.editor, 3u);
  host.Menu(KR_MENU_CREATE);
  for (const auto label :
       {"New Workspace", "New Folder", "New Split View", "New Tab"})
    (void)host.record.Find(label);
  host.Menu(KR_MENU_LIBRARY);
  for (const auto label :
       {"Media", "Downloads", "Mods", "Workspaces", "History"})
    (void)host.record.Find(label);
  host.Menu(KR_MENU_SITE_DATA);
  for (const auto label :
       {"Share Page", "Reader View", "Take Screenshot", "Bookmark Current Tab",
        "Manage", "Site Settings", "Clear Site Data..."})
    (void)host.record.Find(label);
  const auto action = std::ranges::find(host.record.action_kinds,
                                        std::uint32_t{KR_ACTION_SITE_TOOLS});
  KR_ASSERT(action != host.record.action_kinds.end());
  const auto index =
      static_cast<std::size_t>(action - host.record.action_kinds.begin());
  const auto& table = host.browser.table();
  table.invoke_action(table.self, host.record.action_ids[index], {});
  KR_EXPECT_EQ(host.record.context, std::uint32_t{KR_MENU_SITE_DATA});
}
}  // namespace
}  // namespace kr::fixtures

namespace kr::fixtures {
KR_TEST(FixtureLayoutsGroupOrientationsAndPreserveVerticalChoice) {
  Host host;
  host.Menu();
  const auto horizontal = host.record.Find("Horizontal Tabs");
  const auto vertical = host.record.Find("Vertical Tabs");
  KR_EXPECT(horizontal.child != vertical.child);
  KR_EXPECT(horizontal.type == KR_MENU_ITEM_SUBMENU);
  host.Choose("Only Sidebar");
  host.Menu();
  host.Choose("Dynamic");
  KR_EXPECT((host.browser.appearance().layout_flags & KR_LAYOUT_TABS_DYNAMIC) !=
            0);
  KR_EXPECT(
      (host.browser.appearance().layout_flags & KR_LAYOUT_TOOLBAR_SINGLE) != 0);
  host.Menu();
  KR_EXPECT((host.record.Find("Dynamic").flags & KR_MENU_ITEM_CHECKED) != 0);
  KR_EXPECT((host.record.Find("Only Sidebar").flags & KR_MENU_ITEM_CHECKED) ==
            0);
  host.Choose("Collapsed Sidebar");
  KR_EXPECT((host.browser.appearance().layout_flags &
             (KR_LAYOUT_TABS_CLASSIC | KR_LAYOUT_TABS_COMPACT |
              KR_LAYOUT_TABS_DYNAMIC)) == 0);
  KR_EXPECT((host.browser.appearance().layout_flags &
             KR_LAYOUT_TOOLBAR_COLLAPSED) != 0);
}

KR_TEST(FixtureLayoutMenuTextAndAppearanceOptionsRemainCustomizable) {
  Host host;
  KR_ASSERT(host.browser.SetUiText("layout.horizontal_tabs",
                                   "Long translated horizontal tab layouts"));
  host.Menu();
  KR_EXPECT(host.record.Find("Long translated horizontal tab layouts").child >=
            0);
  host.Choose("Classic");
  host.Menu();
  host.Choose("Centered Address Bar");
  KR_EXPECT((host.browser.appearance().layout_flags &
             KR_LAYOUT_CENTERED_ADDRESS) != 0);
  host.Menu();
  host.Choose("Minimal Address Bar");
  KR_EXPECT((host.browser.appearance().layout_flags &
             KR_LAYOUT_MINIMAL_ADDRESS) != 0);
  host.Menu();
  host.Choose("Rounded Frame");
  KR_EXPECT((host.browser.appearance().layout_flags & KR_LAYOUT_SQUARE_FRAME) !=
            0);
}
}  // namespace kr::fixtures
