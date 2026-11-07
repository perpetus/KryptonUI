#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "krypton/fixtures/mock_browser.hpp"
#include "krypton/testing/test.hpp"
#include "krypton/ui/design/tokens.generated.hpp"

namespace kr::fixtures {
namespace {

std::string Copy(KrStr text) {
  return text.data ? std::string(text.data, text.size) : std::string();
}

struct TabSnapshot {
  KrTabId id = 0;
  KrSpaceId space = 0;
  std::uint32_t flags = 0;
  std::string title;
  std::string url;
};

TabSnapshot Copy(const KrTab& tab) {
  KR_EXPECT_EQ(tab.struct_size, sizeof(KrTab));
  return {tab.id, tab.space, tab.flags, Copy(tab.title), Copy(tab.url)};
}

struct MenuItemSnapshot {
  std::int32_t command = 0;
  std::uint32_t type = 0;
  std::uint32_t flags = 0;
  std::string label;
  KrImageId icon = 0;
  std::int32_t submenu = -1;

  [[nodiscard]] bool checked() const {
    return (flags & KR_MENU_ITEM_CHECKED) != 0;
  }
};

struct UiRecorder {
  KrAppearancePrefs appearance{};
  std::uint32_t settings_section = KR_SETTINGS_GENERAL;
  int settings_opens = 0;
  KrTheme theme{.struct_size = sizeof(KrTheme)};
  KrLayoutPrefs layout{.struct_size = sizeof(KrLayoutPrefs)};
  KrSpaceId active_space = 0;
  KrTabId active_tab = 0;
  std::vector<KrColor> space_colors;
  std::vector<TabSnapshot> tabs;
  std::vector<std::vector<MenuItemSnapshot>> menus;
  std::vector<KrImageId> images;
  std::uint32_t menu_request = 0;
  std::uint32_t preview_request = 0;
  KrTabPreview preview{};
  std::vector<KrImageId> dropped_images;
  std::array<KrWorkspaceTheme, 3> workspace_themes{};
  KrSpaceId editor_target = 0;
  int editor_opens = 0;

  [[nodiscard]] const TabSnapshot* Tab(KrTabId id) const {
    const auto item = std::ranges::find(tabs, id, &TabSnapshot::id);
    return item == tabs.end() ? nullptr : &*item;
  }

  [[nodiscard]] const MenuItemSnapshot* Item(std::string_view label) const {
    for (const auto& menu : menus) {
      const auto item =
          std::ranges::find(menu, label, &MenuItemSnapshot::label);
      if (item != menu.end())
        return &*item;
    }
    return nullptr;
  }
};

UiRecorder& Recorder(void* self) {
  return *static_cast<UiRecorder*>(self);
}

KrShellUI MakeUi(UiRecorder& recorder,
                 bool theme_editor = false,
                 bool settings_window = false) {
  KrShellUI ui{};
  ui.struct_size = sizeof(ui);
  ui.self = &recorder;
  ui.destroy = [](void*) {};
  ui.set_window_state = [](void*, const KrWindowState*) {};
  ui.set_theme = [](void* self, const KrTheme* theme) {
    Recorder(self).theme = *theme;
  };
  ui.set_layout_prefs = [](void* self, const KrLayoutPrefs* layout) {
    Recorder(self).layout = *layout;
  };
  ui.put_image = [](void* self, const KrImage* image) {
    Recorder(self).images.push_back(image->id);
  };
  ui.drop_image = [](void* self, KrImageId image) {
    Recorder(self).dropped_images.push_back(image);
  };
  ui.set_tab_preview = [](void* self, std::uint32_t request,
                          const KrTabPreview* preview) {
    Recorder(self).preview_request = request;
    Recorder(self).preview = *preview;
    return KR_OK;
  };
  ui.set_spaces = [](void* self, const KrSpace* spaces, std::size_t count,
                     std::size_t, KrSpaceId active) {
    auto& output = Recorder(self);
    output.active_space = active;
    output.space_colors.clear();
    for (std::size_t index = 0; index < count; ++index)
      output.space_colors.push_back(spaces[index].color);
  };
  ui.tab_inserted = [](void* self, std::size_t index, const KrTab* tab) {
    auto& tabs = Recorder(self).tabs;
    tabs.insert(tabs.begin() +
                    static_cast<std::ptrdiff_t>(std::min(index, tabs.size())),
                Copy(*tab));
  };
  ui.tab_updated = [](void* self, const KrTab* tab) {
    auto& tabs = Recorder(self).tabs;
    const auto item = std::ranges::find(tabs, tab->id, &TabSnapshot::id);
    if (item != tabs.end())
      *item = Copy(*tab);
  };
  ui.tab_moved = [](void*, KrTabId, std::size_t) {};
  ui.tab_removed = [](void* self, KrTabId tab) {
    std::erase_if(Recorder(self).tabs,
                  [=](const auto& value) { return value.id == tab; });
  };
  ui.set_active_tab = [](void* self, KrTabId tab) {
    Recorder(self).active_tab = tab;
  };
  ui.set_nav_state = [](void*, const KrNavState*) {};
  ui.set_actions = [](void*, const KrAction*, std::size_t, std::size_t) {};
  ui.set_suggestions = [](void*, std::uint32_t, const KrSuggestion*,
                          std::size_t, std::size_t) {};
  ui.show_menu = [](void* self, std::uint32_t request, const KrMenu* menus,
                    std::size_t count, std::size_t) {
    auto& output = Recorder(self);
    output.menu_request = request;
    output.menus.clear();
    output.menus.resize(count);
    for (std::size_t menu = 0; menu < count; ++menu) {
      for (std::size_t index = 0; index < menus[menu].count; ++index) {
        const auto& item = menus[menu].items[index];
        KR_EXPECT_EQ(menus[menu].struct_size, sizeof(KrMenu));
        KR_EXPECT_EQ(menus[menu].item_size, sizeof(KrMenuItem));
        KR_EXPECT_EQ(item.struct_size, sizeof(KrMenuItem));
        output.menus[menu].push_back({item.command_id, item.type, item.flags,
                                      Copy(item.label), item.icon,
                                      item.submenu});
      }
    }
  };
  if (settings_window) {
    ui.set_appearance_prefs = [](void* self, const KrAppearancePrefs* prefs) {
      Recorder(self).appearance = *prefs;
      return KR_OK;
    };
    ui.show_settings = [](void* self, std::uint32_t section) {
      auto& output = Recorder(self);
      output.settings_section = section;
      ++output.settings_opens;
      return KR_OK;
    };
  }
  ui.focus_address = [](void*, bool) {};
  ui.pre_translate_message = [](void*, const void*) { return false; };
  if (theme_editor) {
    ui.set_workspace_theme = [](void* self, const KrWorkspaceTheme* theme) {
      if (!theme->space ||
          theme->space > Recorder(self).workspace_themes.size())
        return KR_ERR_INVALID_ARGUMENT;
      Recorder(self).workspace_themes[theme->space - 1] = *theme;
      return KR_OK;
    };
    ui.show_theme_editor = [](void* self, KrSpaceId space) {
      Recorder(self).editor_target = space;
      ++Recorder(self).editor_opens;
      return KR_OK;
    };
  }
  return ui;
}

class Host {
 public:
  explicit Host(MockOptions options = {},
                bool theme_editor = false,
                bool settings_window = false)
      : browser(options,
                {.appearance_changed = [this](const AppearancePrefs& prefs) {
                  persisted = prefs;
                  ++appearance_changes;
                }}) {
    browser.Attach(MakeUi(ui, theme_editor, settings_window));
  }

  ~Host() { browser.Detach(); }

  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;

  void Request(std::uint32_t kind = KR_MENU_SETTINGS,
               std::uint64_t target = 0) {
    ui.menus.clear();
    const auto& table = browser.table();
    table.request_menu(table.self, next_request_++, kind, target, {});
    KR_ASSERT(!ui.menus.empty());
  }

  void Choose(std::string_view label,
              std::uint32_t kind = KR_MENU_SETTINGS,
              std::uint64_t target = 0) {
    Request(kind, target);
    const auto* item = ui.Item(label);
    KR_ASSERT(item != nullptr);
    KR_ASSERT((item->flags & KR_MENU_ITEM_ENABLED) != 0);
    KR_ASSERT(item->type == KR_MENU_ITEM_COMMAND ||
              item->type == KR_MENU_ITEM_CHECK ||
              item->type == KR_MENU_ITEM_RADIO);
    const auto command = item->command;
    const auto& table = browser.table();
    table.menu_command(table.self, ui.menu_request, command);
  }

  void Switch(KrSpaceId space) {
    const auto& table = browser.table();
    table.switch_space(table.self, space);
  }

  UiRecorder ui;
  AppearancePrefs persisted;
  int appearance_changes = 0;
  MockBrowser browser;

 private:
  std::uint32_t next_request_ = 1;
};

KR_TEST(
    FixtureThemeEditorPreviewsWithoutPersistingAndCommitsTheTargetWorkspace) {
  Host host({}, true);
  host.Switch(2);
  host.Choose("Change Theme Colors");
  KR_EXPECT_EQ(host.ui.editor_target, 2u);
  KR_EXPECT_EQ(host.ui.editor_opens, 1);
  KR_EXPECT_EQ(host.appearance_changes, 0);
  auto theme = host.ui.workspace_themes[1];
  theme.flags = KR_THEME_CUSTOMIZED;
  theme.point_count = 1;
  theme.points[0] = {0.625f, 0.375f, 0xFF773399};
  theme.opacity = 0.625f;
  theme.texture = 0.375f;
  theme.scheme = KR_THEME_DARK;
  const auto& table = host.browser.table();
  table.set_workspace_theme(table.self, &theme, false);
  KR_EXPECT_EQ(host.appearance_changes, 0);
  KR_EXPECT(host.browser.dark());
  KR_EXPECT_EQ(host.ui.workspace_themes[1].points[0].color,
               theme.points[0].color);
  KR_EXPECT_EQ(host.ui.workspace_themes[1].texture, theme.texture);
  KR_EXPECT_EQ(host.ui.space_colors[1], theme.points[0].color);
  table.set_workspace_theme(table.self, &theme, true);
  KR_EXPECT_EQ(host.appearance_changes, 1);
  KR_EXPECT_EQ(host.persisted.workspace_themes[1].opacity, theme.opacity);
  KR_EXPECT_EQ(host.persisted.workspace_colors[1], theme.points[0].color);
  KR_EXPECT_EQ(host.persisted.workspace_themes[0].flags, 0u);
  const auto before = host.browser.appearance().workspace_themes[1].opacity;
  theme.opacity = std::numeric_limits<float>::quiet_NaN();
  table.set_workspace_theme(table.self, &theme, true);
  KR_EXPECT_EQ(host.appearance_changes, 1);
  KR_EXPECT_EQ(host.browser.appearance().workspace_themes[1].opacity, before);
}

KR_TEST(FixtureBrowserLayoutsRestoreHiddenSidebarAndPreserveOtherChoices) {
  for (const auto label :
       {"Only Sidebar", "Sidebar and Top Toolbar", "Collapsed Sidebar"}) {
    MockOptions options;
    options.appearance.sidebar_width = 247;
    options.appearance.layout_flags =
        KR_LAYOUT_SIDEBAR_COLLAPSED | KR_LAYOUT_SIDEBAR_AUTOHIDE |
        KR_LAYOUT_SIDEBAR_RIGHT | KR_LAYOUT_COMPACT_MODE |
        KR_LAYOUT_COMPACT_HIDE_TOOLBAR;
    Host host(options);
    host.Choose(label);
    KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_SIDEBAR_COLLAPSED) == 0);
    KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_SIDEBAR_AUTOHIDE) == 0);
    KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_SIDEBAR_RIGHT) != 0);
    KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_COMPACT_MODE) != 0);
    KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_COMPACT_HIDE_TOOLBAR) != 0);
    KR_EXPECT_EQ(host.ui.layout.sidebar_width, 247.0f);
    host.Request();
    KR_ASSERT(host.ui.Item("Browser Layout") != nullptr);
    KR_ASSERT(host.ui.Item(label) != nullptr);
    KR_EXPECT(host.ui.Item(label)->checked());
  }
}

KR_TEST(FixtureToolbarLayoutChoicesPushTheMatchingHostPreferences) {
  Host host;
  host.Choose("Sidebar on the right");
  host.Choose("Only Sidebar");
  KR_EXPECT_EQ(host.ui.layout.flags, std::uint32_t{KR_LAYOUT_SIDEBAR_RIGHT |
                                                   KR_LAYOUT_TOOLBAR_SINGLE});
  host.Request();
  KR_ASSERT(host.ui.Item("Only Sidebar") != nullptr);
  KR_EXPECT(host.ui.Item("Only Sidebar")->checked());
  KR_EXPECT(!host.ui.Item("Sidebar and Top Toolbar")->checked());
  KR_EXPECT(!host.ui.Item("Collapsed Sidebar")->checked());

  host.Choose("Collapsed Sidebar");
  KR_EXPECT_EQ(
      host.ui.layout.flags,
      std::uint32_t{KR_LAYOUT_SIDEBAR_RIGHT | KR_LAYOUT_TOOLBAR_COLLAPSED |
                    KR_LAYOUT_SIDEBAR_ICON_ONLY});
  host.Request();
  KR_EXPECT(host.ui.Item("Collapsed Sidebar")->checked());
  KR_EXPECT(!host.ui.Item("Only Sidebar")->checked());

  host.Choose("Sidebar and Top Toolbar");
  KR_EXPECT_EQ(host.ui.layout.flags, std::uint32_t{KR_LAYOUT_SIDEBAR_RIGHT});
  KR_EXPECT_EQ(host.browser.appearance().layout_flags, host.ui.layout.flags);
  KR_EXPECT_EQ(host.persisted.layout_flags, host.ui.layout.flags);
}

KR_TEST(FixtureCollapsedLayoutCanExpandAndCollapseFromTheSidebarToggle) {
  Host host;
  host.Choose("Collapsed Sidebar");
  host.browser.ToggleSidebar();
  KR_EXPECT_EQ(host.ui.layout.flags &
                   (KR_LAYOUT_TOOLBAR_COLLAPSED | KR_LAYOUT_SIDEBAR_ICON_ONLY),
               0u);
  host.browser.ToggleSidebar();
  KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_SIDEBAR_ICON_ONLY) != 0);
  KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_TOOLBAR_COLLAPSED) == 0);
  host.Choose("Collapse sidebar");
  KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_SIDEBAR_ICON_ONLY) == 0);
}

KR_TEST(FixtureCompactChoicesPreserveTheirTargetWhenToggledOffAndOn) {
  Host host;
  constexpr std::uint32_t targets =
      KR_LAYOUT_COMPACT_HIDE_SIDEBAR | KR_LAYOUT_COMPACT_HIDE_TOOLBAR;
  host.Choose("Sidebar on the right");
  for (const auto& choice :
       std::array{std::pair{"Hide Tab bar",
                            std::uint32_t{KR_LAYOUT_COMPACT_HIDE_SIDEBAR}},
                  std::pair{"Hide Top bar",
                            std::uint32_t{KR_LAYOUT_COMPACT_HIDE_TOOLBAR}},
                  std::pair{"Hide Both", targets}}) {
    host.Choose(choice.first);
    KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_COMPACT_MODE) != 0);
    KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_SIDEBAR_RIGHT) != 0);
    KR_EXPECT_EQ(host.ui.layout.flags & targets, choice.second);
    host.Request();
    KR_ASSERT(host.ui.Item(choice.first) != nullptr);
    KR_EXPECT(host.ui.Item(choice.first)->checked());

    host.Choose("Enable Compact Mode");
    KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_COMPACT_MODE) == 0);
    KR_EXPECT_EQ(host.ui.layout.flags & targets, choice.second);
    host.browser.ToggleCompactMode();
    KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_COMPACT_MODE) != 0);
    KR_EXPECT_EQ(host.ui.layout.flags & targets, choice.second);
  }
}

KR_TEST(FixtureCollapsingOnlySidebarKeepsNavigationInATopToolbar) {
  Host host;
  host.Choose("Only Sidebar");
  host.browser.ToggleSidebar();
  KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_TOOLBAR_SINGLE) == 0);
  KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_TOOLBAR_COLLAPSED) != 0);
  KR_EXPECT((host.ui.layout.flags & KR_LAYOUT_SIDEBAR_ICON_ONLY) != 0);
  host.Request();
  KR_ASSERT(host.ui.Item("Collapsed Sidebar") != nullptr);
  KR_EXPECT(host.ui.Item("Collapsed Sidebar")->checked());
}

KR_TEST(FixtureThemeSystemTracksOsChangesAndExplicitSchemesOverrideThem) {
  Host host;
  KR_EXPECT(!host.ui.theme.dark);
  host.browser.SetSystemDark(true);
  KR_EXPECT(host.ui.theme.dark);
  host.Choose("Light", KR_MENU_THEME);
  KR_EXPECT(!host.ui.theme.dark);
  KR_EXPECT(host.browser.appearance().scheme == ThemeScheme::kLight);
  host.browser.SetSystemDark(false);
  host.browser.SetSystemDark(true);
  KR_EXPECT(!host.ui.theme.dark);

  host.Choose("Dark", KR_MENU_THEME);
  host.browser.SetSystemDark(false);
  KR_EXPECT(host.ui.theme.dark);
  host.Choose("System", KR_MENU_THEME);
  KR_EXPECT(!host.ui.theme.dark);
  host.browser.SetSystemDark(true);
  KR_EXPECT(host.ui.theme.dark);
  KR_EXPECT(host.persisted.scheme == ThemeScheme::kSystem);
}

KR_TEST(FixtureThemePresetsUseReferenceSwatchesAndBelongToEachWorkspace) {
  Host host;
  const auto& first = design::kTokens.theme_presets.front();
  const auto& second = design::kTokens.theme_presets.back();
  host.Request(KR_MENU_THEME);
  const auto* item = host.ui.Item(first.name);
  KR_ASSERT(item != nullptr);
  KR_EXPECT(item->icon != 0);
  KR_EXPECT(std::ranges::find(host.ui.images, item->icon) !=
            host.ui.images.end());
  host.Choose(first.name, KR_MENU_THEME);
  KR_ASSERT(host.ui.space_colors.size() == 3);
  KR_EXPECT_EQ(host.ui.space_colors[0], first.color);
  KR_EXPECT_EQ(host.ui.space_colors[1], 0u);
  KR_EXPECT_EQ(host.ui.theme.accent, first.color);

  host.Switch(2);
  host.Choose(second.name, KR_MENU_THEME);
  KR_EXPECT_EQ(host.ui.space_colors[0], first.color);
  KR_EXPECT_EQ(host.ui.space_colors[1], second.color);
  KR_EXPECT_EQ(host.ui.space_colors[2], 0u);
  KR_EXPECT_EQ(host.ui.theme.accent, second.color);
  host.Switch(1);
  KR_EXPECT_EQ(host.ui.theme.accent, first.color);
  KR_EXPECT_EQ(host.browser.appearance().workspace_colors[0], first.color);
  KR_EXPECT_EQ(host.browser.appearance().workspace_colors[1], second.color);
  host.Request(KR_MENU_THEME);
  KR_EXPECT(host.ui.Item(first.name)->checked());
  host.Choose("Reset workspace colors", KR_MENU_THEME);
  KR_EXPECT_EQ(host.ui.space_colors[0], 0u);
  KR_EXPECT_EQ(host.ui.space_colors[1], second.color);
  KR_EXPECT_EQ(host.persisted.workspace_colors[1], second.color);
}

KR_TEST(FixtureTabContextMenusPinMuteDuplicateCloseAndReopenRealTabs) {
  Host host;
  const auto source = host.ui.active_tab;
  const auto* original = host.ui.Tab(source);
  KR_ASSERT(original != nullptr);
  const auto original_title = original->title;
  const auto original_url = original->url;
  const auto count = host.ui.tabs.size();
  host.Choose("Pin Tab", KR_MENU_TAB, source);
  KR_ASSERT(host.ui.Tab(source) != nullptr);
  KR_EXPECT((host.ui.Tab(source)->flags & KR_TAB_PINNED) != 0);
  host.Choose("Mute Tab", KR_MENU_TAB, source);
  KR_EXPECT((host.ui.Tab(source)->flags & KR_TAB_MUTED) != 0);
  host.Request(KR_MENU_TAB, source);
  KR_EXPECT(host.ui.Item("Pin Tab")->checked());
  KR_EXPECT(host.ui.Item("Mute Tab")->checked());

  host.Choose("Duplicate Tab", KR_MENU_TAB, source);
  KR_EXPECT_EQ(host.ui.tabs.size(), count + 1);
  const auto duplicate = host.ui.active_tab;
  KR_EXPECT(duplicate != source);
  KR_ASSERT(host.ui.Tab(duplicate) != nullptr);
  KR_EXPECT_EQ(host.ui.Tab(duplicate)->title, original_title);
  KR_EXPECT_EQ(host.ui.Tab(duplicate)->url, original_url);

  host.Choose("Close Tab", KR_MENU_TAB, duplicate);
  KR_EXPECT_EQ(host.ui.tabs.size(), count);
  KR_EXPECT(host.ui.Tab(duplicate) == nullptr);
  host.Choose("Reopen Closed Tab", KR_MENU_SIDEBAR);
  KR_EXPECT_EQ(host.ui.tabs.size(), count + 1);
  KR_EXPECT_EQ(host.ui.active_tab, duplicate);
  KR_ASSERT(host.ui.Tab(duplicate) != nullptr);
  KR_EXPECT_EQ(host.ui.Tab(duplicate)->url, original_url);
}

KR_TEST(FixtureAppearanceCanRestoreSerializableHostPreferences) {
  Host host;
  AppearancePrefs saved;
  saved.scheme = ThemeScheme::kDark;
  saved.layout_flags = KR_LAYOUT_TOOLBAR_COLLAPSED | KR_LAYOUT_SIDEBAR_RIGHT |
                       KR_LAYOUT_COMPACT_MODE | KR_LAYOUT_COMPACT_HIDE_TOOLBAR;
  saved.sidebar_width = 287;
  saved.workspace_colors = {0xFF112233, 0xFF445566, 0xFF778899};
  host.browser.ApplyAppearance(saved);
  KR_EXPECT(host.ui.theme.dark);
  KR_EXPECT_EQ(host.ui.layout.flags, saved.layout_flags);
  KR_EXPECT_EQ(host.ui.layout.sidebar_width, saved.sidebar_width);
  KR_EXPECT(host.browser.appearance().workspace_colors ==
            saved.workspace_colors);
  KR_EXPECT(host.persisted.workspace_colors == saved.workspace_colors);

  Host restored({.appearance = host.persisted});
  KR_EXPECT(restored.ui.theme.dark);
  KR_EXPECT_EQ(restored.ui.layout.flags, saved.layout_flags);
  KR_EXPECT_EQ(restored.ui.layout.sidebar_width, saved.sidebar_width);
  KR_EXPECT(restored.ui.space_colors ==
            (std::vector<KrColor>{saved.workspace_colors.begin(),
                                  saved.workspace_colors.end()}));
}

KR_TEST(FixtureTabPreviewIsSampleDataAndStaleCancelKeepsCurrentImage) {
  Host host;
  const auto& table = host.browser.table();
  KR_ASSERT(table.request_tab_preview && table.cancel_tab_preview);
  KR_ASSERT(host.ui.tabs.size() >= 2);
  const auto first = host.ui.tabs[0].id;
  const auto second = host.ui.tabs[1].id;
  table.request_tab_preview(table.self, 1, first);
  KR_EXPECT_EQ(host.ui.preview_request, 1u);
  KR_EXPECT_EQ(host.ui.preview.tab, first);
  KR_EXPECT(host.ui.preview.thumbnail != 0);
  KR_EXPECT((host.ui.preview.flags & KR_TAB_PREVIEW_SAMPLE) != 0);
  KR_EXPECT((host.ui.preview.flags & KR_TAB_PREVIEW_MEMORY_KNOWN) != 0);
  KR_EXPECT_EQ(host.ui.preview.memory_usage_bytes, 42'000'000u);
  table.request_tab_preview(table.self, 2, second);
  KR_EXPECT_EQ(host.ui.preview_request, 2u);
  KR_EXPECT_EQ(host.ui.preview.tab, second);
  const auto drops = host.ui.dropped_images.size();
  table.cancel_tab_preview(table.self, 1);
  KR_EXPECT_EQ(host.ui.dropped_images.size(), drops);
  table.cancel_tab_preview(table.self, 2);
  KR_EXPECT_EQ(host.ui.dropped_images.size(), drops + 1);
  KR_EXPECT_EQ(host.ui.dropped_images.back(), host.ui.preview.thumbnail);
  table.cancel_tab_preview(table.self, 2);
  KR_EXPECT_EQ(host.ui.dropped_images.size(), drops + 1);
}

}  // namespace
}  // namespace kr::fixtures

namespace kr::fixtures {
KR_TEST(FixtureFallbackSettingsGroupsHorizontalAndVerticalTabs) {
  Host host;
  host.Request();
  const auto* horizontal = host.ui.Item("Horizontal Tabs");
  const auto* vertical = host.ui.Item("Vertical Tabs");
  KR_ASSERT(horizontal != nullptr && vertical != nullptr);
  KR_EXPECT(horizontal->submenu != vertical->submenu);
  host.Choose("Classic");
  KR_EXPECT((host.browser.appearance().layout_flags & KR_LAYOUT_TABS_CLASSIC) !=
            0);
  host.Choose("Only Sidebar");
  KR_EXPECT((host.browser.appearance().layout_flags &
             (KR_LAYOUT_TABS_CLASSIC | KR_LAYOUT_TABS_COMPACT |
              KR_LAYOUT_TABS_DYNAMIC)) == 0);
}
}  // namespace kr::fixtures

namespace kr::fixtures {
KR_TEST(BlankScreenshotPageIsEmptyAndTitledKrypton) {
  Host host;
  const auto& port = host.browser.table();
  constexpr std::string_view url = "krypton://blank";
  port.navigate(port.self, {url.data(), url.size()}, KR_OPEN_CURRENT_TAB);
  const auto blank = host.browser.active_page();
  KR_EXPECT(blank.blank);
  KR_EXPECT_EQ(blank.title, "Krypton");
  KR_EXPECT_EQ(blank.url, url);
  constexpr std::string_view ordinary = "https://example.test";
  port.navigate(port.self, {ordinary.data(), ordinary.size()},
                KR_OPEN_CURRENT_TAB);
  KR_EXPECT(!host.browser.active_page().blank);
}
}  // namespace kr::fixtures

namespace kr::fixtures {
KR_TEST(SettingsHostOpensAPopoutWithoutCreatingATabAndConfirmsSavedAppearance) {
  Host host({}, false, true);
  const auto tab_count = host.ui.tabs.size();
  host.browser.OpenSettings();
  KR_EXPECT_EQ(host.ui.settings_opens, 1);
  KR_EXPECT_EQ(host.ui.settings_section,
               static_cast<std::uint32_t>(KR_SETTINGS_CURRENT));
  KR_EXPECT_EQ(host.ui.tabs.size(), tab_count);
  const auto& port = host.browser.table();
  const KrAppearancePrefs prefs{sizeof(KrAppearancePrefs), KR_COLOR_DARK,
                                KR_MENU_MATERIAL_ACRYLIC,
                                KR_APPEARANCE_HIDE_HOVER_IMAGES};
  port.set_appearance_prefs(port.self, &prefs);
  KR_EXPECT_EQ(host.persisted.menu_material,
               static_cast<std::uint32_t>(KR_MENU_MATERIAL_ACRYLIC));
  KR_EXPECT_EQ(host.persisted.appearance_flags,
               static_cast<std::uint32_t>(KR_APPEARANCE_HIDE_HOVER_IMAGES));
  KR_EXPECT(host.persisted.scheme == ThemeScheme::kDark);
  KR_EXPECT_EQ(host.ui.appearance.color_scheme,
               static_cast<std::uint32_t>(KR_COLOR_DARK));
  constexpr std::string_view privacy = "krypton://settings/privacy";
  port.navigate(port.self, {privacy.data(), privacy.size()},
                KR_OPEN_CURRENT_TAB);
  KR_EXPECT_EQ(host.ui.settings_section,
               static_cast<std::uint32_t>(KR_SETTINGS_PRIVACY));
  KR_EXPECT_EQ(host.ui.tabs.size(), tab_count);
}
KR_TEST(FixtureWorkspaceEditsGrowBeyondSeededThemeSlotsAndPreserveTabs) {
  Host host;
  const auto& port = host.browser.table();
  const auto initial_tabs = host.ui.tabs.size();
  const std::string name = "Extra workspace";
  const KrSpace created{
      sizeof(KrSpace), 0, {name.data(), name.size()}, {}, 0xFF667788};
  for (int index = 0; index < 4; ++index)
    port.edit_workspace(port.self, KR_WORKSPACE_UPSERT, &created);
  KR_EXPECT_EQ(host.ui.space_colors.size(), std::size_t{7});
  const KrAppearancePrefs dark{sizeof(KrAppearancePrefs), KR_COLOR_DARK,
                               KR_MENU_MATERIAL_DEFAULT, 0};
  port.set_appearance_prefs(port.self, &dark);
  KR_EXPECT_EQ(host.ui.space_colors.size(), std::size_t{7});
  host.Switch(4);
  KR_EXPECT_EQ(host.ui.active_space, KrSpaceId{4});
  KR_EXPECT_EQ(host.ui.active_tab, KrTabId{0});
  const KrSpace removed{.struct_size = sizeof(KrSpace), .id = 4};
  port.edit_workspace(port.self, KR_WORKSPACE_REMOVE, &removed);
  KR_EXPECT_EQ(host.ui.space_colors.size(), std::size_t{6});
  KR_EXPECT_EQ(host.ui.active_space, KrSpaceId{1});
  KR_EXPECT_EQ(host.ui.tabs.size(), initial_tabs);
  const KrSpace default_space{.struct_size = sizeof(KrSpace), .id = 1};
  port.edit_workspace(port.self, KR_WORKSPACE_REMOVE, &default_space);
  KR_EXPECT_EQ(host.ui.space_colors.size(), std::size_t{6});
}

KR_TEST(SettingsHostRetainsLegacySettingsNavigationWhenTheUISlotIsMissing) {
  Host host;
  host.browser.OpenSettings();
  KR_EXPECT_EQ(host.browser.active_page().url, "krypton://settings/appearance");
}
}  // namespace kr::fixtures
