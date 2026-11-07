// Drives the module through abi.h exactly as a host would, with the headless
// platform underneath.

#include "krypton_ui/abi.h"

#include <optional>
#include <string>
#include <vector>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"

namespace kr {
namespace {

static_assert(KR_UI_ABI_VERSION == 2u);
static_assert(KR_LAYOUT_TOOLBAR_SINGLE == 1u << 3);
static_assert(KR_LAYOUT_TOOLBAR_COLLAPSED == 1u << 4);
static_assert(KR_LAYOUT_SIDEBAR_ICON_ONLY == 1u << 5);
static_assert(KR_LAYOUT_COMPACT_MODE == 1u << 6);
static_assert(KR_LAYOUT_COMPACT_HIDE_SIDEBAR == 1u << 7);
static_assert(KR_LAYOUT_COMPACT_HIDE_TOOLBAR == 1u << 8);
static_assert(KR_MENU_SETTINGS == 8 && KR_MENU_THEME == 9);
static_assert(KR_ELEMENT_TOOLBAR == 9 && KR_ELEMENT_SIDEBAR_TOGGLE == 10);

// A host that records what the module asks for.
struct Recorder {
  int content_bounds_calls = 0;
  KrRect content_bounds{};
  float content_radius = 0;
  std::vector<KrRect> drag_regions;
  std::uint32_t query_id = 0;
  std::string query;
  std::optional<std::pair<std::uint32_t, std::size_t>> accepted;
  std::string navigated;
  std::uint32_t navigated_disposition = 99;
  int focus_page_calls = 0;
  std::uint32_t menu_request = 0;
  std::uint32_t menu_kind = 99;
  int anchor_calls = 0;
  bool last_anchor_visible = true;
  std::vector<std::string> logs;
  // close_tab and new_tab calls in order: a tab id, or 0 for a new tab.
  std::vector<KrTabId> tab_edits;
};

Recorder& R(void* self) {
  return *static_cast<Recorder*>(self);
}
std::string S(KrStr s) {
  return {s.data, s.size};
}
KrStr Str(std::string_view s) {
  return {s.data(), s.size()};
}

KrBrowser MakeBrowser(Recorder& recorder) {
  KrBrowser b{};
  b.struct_size = sizeof(KrBrowser);
  b.self = &recorder;
  b.navigate = [](void* self, KrStr text, std::uint32_t disposition) {
    R(self).navigated = S(text);
    R(self).navigated_disposition = disposition;
  };
  b.query_suggestions = [](void* self, std::uint32_t id, KrStr text) {
    R(self).query_id = id;
    R(self).query = S(text);
  };
  b.accept_suggestion = [](void* self, std::uint32_t id, std::size_t index,
                           std::uint32_t) {
    R(self).accepted = std::pair{id, index};
  };
  b.request_menu = [](void* self, std::uint32_t id, std::uint32_t kind,
                      std::uint64_t, KrRect) {
    R(self).menu_request = id;
    R(self).menu_kind = kind;
  };
  b.set_content_bounds = [](void* self, KrRect bounds, float radius) {
    ++R(self).content_bounds_calls;
    R(self).content_bounds = bounds;
    R(self).content_radius = radius;
  };
  b.set_anchor = [](void* self, std::uint32_t, std::uint64_t, KrRect,
                    bool visible) {
    ++R(self).anchor_calls;
    R(self).last_anchor_visible = visible;
  };
  b.set_drag_regions = [](void* self, const KrRect* regions,
                          std::size_t count) {
    R(self).drag_regions.assign(regions, regions + count);
  };
  b.focus_page = [](void* self) { ++R(self).focus_page_calls; };
  b.close_tab = [](void* self, KrTabId tab) {
    R(self).tab_edits.push_back(tab);
  };
  b.new_tab = [](void* self) { R(self).tab_edits.push_back(0); };
  b.log = [](void* self, std::uint32_t, KrStr text) {
    R(self).logs.push_back(S(text));
  };
  return b;
}

struct Host {
  Recorder recorder;
  KrBrowser browser = MakeBrowser(recorder);
  KrShellUI ui{};
  KrResult created = KR_ERR_PLATFORM;

  explicit Host(std::size_t browser_size = sizeof(KrBrowser)) {
    headless::Reset();
    ui.struct_size = sizeof(ui);
    browser.struct_size = browser_size;
    const KrCreateParams params{.struct_size = sizeof(KrCreateParams),
                                .abi_version = KR_UI_ABI_VERSION,
                                .parent_window = nullptr,
                                .browser = &browser};
    created = kr_ui_create(&params, &ui);
  }
  ~Host() {
    if (created == KR_OK)
      ui.destroy(ui.self);
    headless::RunPendingTasks();  // must be harmless after destroy
  }

  void Window(float width, float height) {
    const KrWindowState state{.struct_size = sizeof(KrWindowState),
                              .flags = KR_WINDOW_ACTIVE,
                              .scale_factor = 1,
                              .client = {0, 0, width, height}};
    ui.set_window_state(ui.self, &state);
  }
  void Tab(std::size_t index,
           KrTabId id,
           std::string_view title,
           std::uint32_t flags = 0) {
    const KrTab tab{.struct_size = sizeof(KrTab),
                    .id = id,
                    .space = 1,
                    .flags = flags,
                    .title = Str(title)};
    ui.tab_inserted(ui.self, index, &tab);
  }
};

KR_TEST(AbiRejectsBadParamsAndVersions) {
  headless::Reset();
  KrShellUI ui{};
  KR_EXPECT_EQ(kr_ui_create(nullptr, &ui), KR_ERR_INVALID_ARGUMENT);

  Recorder recorder;
  const KrBrowser browser = MakeBrowser(recorder);
  KrCreateParams params{.struct_size = sizeof(KrCreateParams),
                        .abi_version = KR_UI_ABI_VERSION + 1,
                        .browser = &browser};
  KR_EXPECT_EQ(kr_ui_create(&params, &ui), KR_ERR_VERSION);
  KR_EXPECT(!kr_ui_is_supported(KR_UI_ABI_VERSION + 1));

  headless::SetSupported(false);
  params.abi_version = KR_UI_ABI_VERSION;
  KR_EXPECT_EQ(kr_ui_create(&params, &ui), KR_ERR_UNSUPPORTED);
  KR_EXPECT(!kr_ui_is_supported(KR_UI_ABI_VERSION));
  headless::SetSupported(true);
  KR_EXPECT(kr_ui_is_supported(KR_UI_ABI_VERSION));
}

KR_TEST(AbiReportsViewCreationFailuresToTheHostLog) {
  headless::Reset();
  headless::SetViewCreationEnabled(false);
  Recorder recorder;
  const KrBrowser browser = MakeBrowser(recorder);
  const KrCreateParams params{.struct_size = sizeof(KrCreateParams),
                              .abi_version = KR_UI_ABI_VERSION,
                              .browser = &browser};
  KrShellUI ui{};
  KR_EXPECT_EQ(kr_ui_create(&params, &ui), KR_ERR_PLATFORM);
  KR_ASSERT(recorder.logs.size() == 1);
  KR_EXPECT(recorder.logs[0].find("headless view creation disabled by test") !=
            std::string::npos);
  KR_EXPECT(headless::Delegate() == nullptr);
  headless::SetViewCreationEnabled(true);
}

KR_TEST(AbiCoalescesManyUpdatesIntoOneRender) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  host.Window(1400, 900);
  for (KrTabId id = 1; id <= 50; ++id)
    host.Tab(id - 1, id, "tab");
  host.ui.set_active_tab(host.ui.self, 7);

  KR_EXPECT_EQ(headless::RenderCount(), 0u);  // nothing until the loop turns
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::RenderCount(), 1u);
  KR_EXPECT_EQ(headless::LastRender().today.size(), 50u);
  KR_EXPECT_EQ(headless::PendingTaskCount(), 0u);
}

KR_TEST(AbiClearTabsClosesIdleUnpinnedTabs) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  host.Tab(0, 1, "pinned", KR_TAB_PINNED);
  host.Tab(1, 2, "selected");
  host.Tab(2, 3, "idle");
  host.Tab(3, 4, "playing", KR_TAB_AUDIBLE);
  host.Tab(4, 5, "calling", KR_TAB_RECORDING);
  host.Tab(5, 6, "sharing", KR_TAB_CAPTURING);
  host.Tab(6, 7, "favorite", KR_TAB_FAVORITE);
  host.Tab(7, 8, "also idle");
  host.ui.set_active_tab(host.ui.self, 2);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().today_idle);

  headless::Delegate()->OnClearTabs();
  KR_EXPECT(host.recorder.tab_edits == std::vector<KrTabId>({3, 8}));
  KR_EXPECT_EQ(headless::LastNotification(),
               UiTextView{}[UiText::kTabsCleared]);
}

KR_TEST(AbiClearTabsShowsItsArrowOnlyWhenEveryUnpinnedTabIsIdle) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  host.Tab(0, 1, "pinned", KR_TAB_PINNED);
  host.Tab(1, 2, "idle");
  host.ui.set_active_tab(host.ui.self, 1);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().today_idle);
  host.ui.set_active_tab(host.ui.self, 2);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().today_idle);
}

KR_TEST(AbiClearTabsClosesEveryTabInUseAndKeepsTheWindow) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  host.Tab(0, 1, "playing", KR_TAB_AUDIBLE);
  host.Tab(1, 2, "selected");
  host.ui.set_active_tab(host.ui.self, 2);
  headless::RunPendingTasks();
  // Every tab is in use, so Clear closes them all, the selected one last.
  // They are the window's only tabs: a new tab is opened first.
  headless::Delegate()->OnClearTabs();
  KR_EXPECT(host.recorder.tab_edits == std::vector<KrTabId>({0, 1, 2}));

  host.recorder.tab_edits.clear();
  host.Tab(2, 3, "pinned", KR_TAB_PINNED);
  headless::Delegate()->OnClearTabs();
  KR_EXPECT(host.recorder.tab_edits == std::vector<KrTabId>({1, 2}));
}

KR_TEST(AbiClearTabsDoesNothingWithoutUnpinnedTabs) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  host.Tab(0, 1, "pinned", KR_TAB_PINNED);
  headless::Delegate()->OnClearTabs();
  KR_EXPECT(host.recorder.tab_edits.empty());
  KR_EXPECT(headless::LastNotification().empty());
}

KR_TEST(AbiReportsContentBoundsOnlyWhenTheyChange) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  host.Window(1400, 900);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.recorder.content_bounds_calls, 1);
  KR_EXPECT_EQ(host.recorder.content_radius, design::kTokens.radius.card);
  KR_EXPECT_EQ(host.recorder.drag_regions.size(), 2u);

  host.Tab(0, 1, "only the tab list changes");
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.recorder.content_bounds_calls, 1);

  host.Window(1000, 700);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.recorder.content_bounds_calls, 2);
  KR_EXPECT_EQ(host.recorder.content_bounds.height,
               700 - design::kTokens.spacing.toolbar_height -
                   design::kTokens.spacing.window_margin);
}

KR_TEST(AbiLayoutsRoundTripWithoutChangingTheContractVersion) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  host.Window(1400, 900);
  KrLayoutPrefs prefs{.struct_size = sizeof(KrLayoutPrefs),
                      .flags = KR_LAYOUT_TOOLBAR_SINGLE};
  host.ui.set_layout_prefs(host.ui.self, &prefs);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().layout.toolbar_layout ==
            ToolbarLayout::kSingle);
  KR_EXPECT(!headless::LastRender().layout.toolbar_visible);

  prefs.flags = KR_LAYOUT_TOOLBAR_COLLAPSED | KR_LAYOUT_SIDEBAR_RIGHT;
  host.ui.set_layout_prefs(host.ui.self, &prefs);
  headless::RunPendingTasks();
  const auto& layout = headless::LastRender().layout;
  KR_EXPECT(layout.toolbar_layout == ToolbarLayout::kCollapsed);
  KR_EXPECT(layout.sidebar_icon_only);
  KR_EXPECT(layout.sidebar_on_right);
  KR_EXPECT_EQ(layout.sidebar.width, design::kTokens.sidebar.collapsed_width);
}

KR_TEST(AbiCompactRevealsCoalesceAndDoNotMoveTheHostPage) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  host.Window(1400, 900);
  const KrLayoutPrefs prefs{.struct_size = sizeof(KrLayoutPrefs),
                            .flags = KR_LAYOUT_COMPACT_MODE |
                                     KR_LAYOUT_COMPACT_HIDE_SIDEBAR |
                                     KR_LAYOUT_COMPACT_HIDE_TOOLBAR};
  host.ui.set_layout_prefs(host.ui.self, &prefs);
  headless::RunPendingTasks();
  const auto bounds_calls = host.recorder.content_bounds_calls;
  const auto renders = headless::RenderCount();
  headless::Delegate()->OnSidebarRevealChanged(true);
  headless::Delegate()->OnToolbarRevealChanged(true);
  KR_EXPECT_EQ(headless::PendingTaskCount(), 1u);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::RenderCount(), renders + 1);
  KR_EXPECT_EQ(host.recorder.content_bounds_calls, bounds_calls);
  KR_EXPECT(headless::LastRender().layout.sidebar_overlays);
  KR_EXPECT(headless::LastRender().layout.toolbar_overlays);

  headless::Delegate()->OnSidebarRevealChanged(true);
  headless::Delegate()->OnToolbarRevealChanged(true);
  KR_EXPECT_EQ(headless::PendingTaskCount(), 0u);
}

KR_TEST(AbiCompactSidebarGracePeriodCancelsAStaleHideOnReentry) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  host.Window(1400, 900);
  const KrLayoutPrefs prefs{.struct_size = sizeof(KrLayoutPrefs),
                            .flags = KR_LAYOUT_COMPACT_MODE};
  host.ui.set_layout_prefs(host.ui.self, &prefs);
  headless::RunPendingTasks();
  const auto page = headless::LastRender().layout.card;
  auto* delegate = headless::Delegate();
  delegate->OnSidebarRevealChanged(true);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().layout.sidebar_visible);
  delegate->OnSidebarRevealChanged(false);
  headless::RunPendingTasks();
  const auto grace = std::chrono::milliseconds(
      static_cast<int>(design::kTokens.compact.sidebar_keep_hover_ms));
  headless::AdvanceTime(grace - std::chrono::milliseconds(1));
  KR_EXPECT(headless::LastRender().layout.sidebar_visible);
  delegate->OnSidebarRevealChanged(true);
  headless::AdvanceTime(std::chrono::milliseconds(1));
  KR_EXPECT(headless::LastRender().layout.sidebar_visible);
  delegate->OnSidebarRevealChanged(false);
  headless::AdvanceTime(grace);
  KR_EXPECT(!headless::LastRender().layout.sidebar_visible);
  KR_EXPECT(headless::LastRender().layout.card == page);
}

KR_TEST(AbiCompactToolbarReentryCancelsAQueuedHide) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  host.Window(1400, 900);
  const KrLayoutPrefs prefs{
      .struct_size = sizeof(KrLayoutPrefs),
      .flags = KR_LAYOUT_COMPACT_MODE | KR_LAYOUT_COMPACT_HIDE_TOOLBAR};
  host.ui.set_layout_prefs(host.ui.self, &prefs);
  headless::RunPendingTasks();
  auto* delegate = headless::Delegate();
  delegate->OnToolbarRevealChanged(true);
  headless::RunPendingTasks();
  delegate->OnToolbarRevealChanged(false);
  delegate->OnToolbarRevealChanged(true);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().layout.toolbar_visible);
  delegate->OnToolbarRevealChanged(false);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().layout.toolbar_visible);
}

KR_TEST(AbiSettingsAndThemeMenuIntentsReachTheHost) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  headless::Delegate()->OnMenuRequested(MenuKind::kSettings, 0, {});
  KR_EXPECT_EQ(host.recorder.menu_kind,
               static_cast<std::uint32_t>(KR_MENU_SETTINGS));
  headless::Delegate()->OnMenuRequested(MenuKind::kTheme, 0, {});
  KR_EXPECT_EQ(host.recorder.menu_kind,
               static_cast<std::uint32_t>(KR_MENU_THEME));
}

KR_TEST(AbiMenuImagesAreAvailableBeforeTheNextRender) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  constexpr KrImageId id = (KrImageId{1} << 63) | 1;
  const std::uint8_t pixels[] = {10, 20, 30, 255};
  const KrImage image{.struct_size = sizeof(KrImage),
                      .id = id,
                      .width = 1,
                      .height = 1,
                      .stride = 4,
                      .pixels = pixels};
  host.ui.put_image(host.ui.self, &image);
  headless::Delegate()->OnMenuRequested(MenuKind::kTheme, 0, {});
  const KrMenuItem item{.struct_size = sizeof(KrMenuItem),
                        .command_id = 1,
                        .type = KR_MENU_ITEM_RADIO,
                        .flags = KR_MENU_ITEM_ENABLED,
                        .label = Str("Theme swatch"),
                        .icon = id,
                        .submenu = -1};
  const KrMenu menu{sizeof(KrMenu), &item, 1, sizeof(KrMenuItem)};
  host.ui.show_menu(host.ui.self, host.recorder.menu_request, &menu, 1,
                    sizeof(KrMenu));
  KR_ASSERT(headless::LastMenu().has_value());
  KR_ASSERT(headless::LastMenu()->images.size() == 1);
  KR_EXPECT_EQ(headless::LastMenu()->images[0].id, ImageId{id});
  KR_EXPECT_EQ(headless::LastMenu()->images[0].pixels[0], std::uint8_t{10});
  KR_EXPECT_EQ(headless::RenderCount(), 0u);

  host.ui.drop_image(host.ui.self, id);
  host.ui.show_menu(host.ui.self, host.recorder.menu_request, &menu, 1,
                    sizeof(KrMenu));
  KR_EXPECT(headless::LastMenu()->images.empty());
}

KR_TEST(AbiMenuPreservesUnusedSourceComponentData) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  headless::Delegate()->OnMenuRequested(MenuKind::kApp, 0, {});
  {
    std::string secondary = "Secondary label";
    const KrStr families[] = {Str("Mapped face"), Str("Fallback face")};
    const KrMenuFont font{
        .struct_size = sizeof(KrMenuFont),
        .family = Str("Requested face"),
        .actual_families = families,
        .actual_family_count = 2,
        .size_pixels = 17,
        .weight = 600,
        .render_flags = KR_MENU_FONT_ANTIALIASING,
        .text_gamma = 1.8f,
    };
    const KrMenuColor colors[] = {
        {sizeof(KrMenuColor), KR_MENU_COLOR_FOREGROUND, 123, true, 0xff123456},
        {sizeof(KrMenuColor), KR_MENU_COLOR_SUBMENU_BACKGROUND, 456, false, 0},
    };
    const KrKeyboardAccelerator key{
        sizeof(KrKeyboardAccelerator), 76, 4, 1, 123456, 7, true};
    const KrMenuItemMetadata metadata{
        .struct_size = sizeof(KrMenuItemMetadata),
        .source_type = 6,
        .source_command_id = 817,
        .source_index = 12,
        .source_button_type = -1,
        .flags = KR_MENU_META_DYNAMIC | KR_MENU_META_ACTIONABLE_SUBMENU,
        .radio_group = 9,
        .separator_type = 4,
        .badge_type = 2,
        .secondary_label = Str(secondary),
        .minor_text = Str("Minor text"),
        .accessible_name = Str("Accessible menu name"),
        .element_identifier = Str("SourceElement"),
        .minor_icon = 91,
        .keyboard_accelerator = &key,
        .fonts = &font,
        .font_count = 1,
        .font_size = sizeof(KrMenuFont),
        .colors = colors,
        .color_count = 2,
        .color_size = sizeof(KrMenuColor),
        .font_height = 23,
    };
    const KrMenuItem item{.struct_size = sizeof(KrMenuItem),
                          .command_id = 817,
                          .type = KR_MENU_ITEM_COMMAND,
                          .label = Str("Source command"),
                          .submenu = -1,
                          .metadata = &metadata};
    const KrMenu menu{sizeof(KrMenu), &item, 1, sizeof(KrMenuItem)};
    host.ui.show_menu(host.ui.self, host.recorder.menu_request, &menu, 1,
                      sizeof(KrMenu));
  }
  KR_ASSERT(headless::LastMenu());
  const auto& item = headless::LastMenu()->menu.menus[0][0];
  KR_ASSERT(item.metadata);
  const auto& metadata = *item.metadata;
  KR_EXPECT_EQ(metadata.secondary_label, std::string("Secondary label"));
  KR_EXPECT_EQ(metadata.source_command_id, 817);
  KR_EXPECT_EQ(metadata.source_index, 12u);
  KR_EXPECT(!metadata.flags.Has(MenuMetadataFlag::kVisible));
  KR_EXPECT(metadata.flags.Has(MenuMetadataFlag::kActionableSubmenu));
  KR_EXPECT_EQ(metadata.radio_group, 9);
  KR_EXPECT_EQ(metadata.minor_icon, ImageId{91});
  KR_ASSERT(metadata.keyboard_accelerator);
  KR_EXPECT_EQ(metadata.keyboard_accelerator->time_ticks_us, 123456);
  KR_EXPECT_EQ(metadata.fonts[0].actual_families[1],
               std::string("Fallback face"));
  KR_EXPECT_EQ(metadata.fonts[0].text_gamma, 1.8f);
  KR_EXPECT_EQ(metadata.font_height, 23);
  KR_EXPECT_EQ(metadata.colors[0].color, std::optional(Color{0xff123456}));
  KR_EXPECT(!metadata.colors[1].color);
}

KR_TEST(AbiMenuLabelsDecodeMnemonicsOnlyWhenFlagged) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  headless::Delegate()->OnMenuRequested(MenuKind::kApp, 0, {});
  const KrMenuItemMetadata mnemonics{
      .struct_size = sizeof(KrMenuItemMetadata),
      .source_button_type = -1,
      .flags = KR_MENU_META_VISIBLE | KR_MENU_META_MAY_HAVE_MNEMONICS};
  const KrMenuItemMetadata literal{.struct_size = sizeof(KrMenuItemMetadata),
                                   .source_button_type = -1,
                                   .flags = KR_MENU_META_VISIBLE};
  const KrMenuItem items[] = {{.struct_size = sizeof(KrMenuItem),
                               .command_id = 1,
                               .label = Str("New &tab"),
                               .submenu = -1,
                               .metadata = &mnemonics},
                              {.struct_size = sizeof(KrMenuItem),
                               .command_id = 2,
                               .label = Str("Q&A"),
                               .submenu = -1,
                               .metadata = &literal}};
  const KrMenu menu{sizeof(KrMenu), items, 2, sizeof(KrMenuItem)};
  host.ui.show_menu(host.ui.self, host.recorder.menu_request, &menu, 1,
                    sizeof(KrMenu));
  KR_ASSERT(headless::LastMenu());
  const auto& rows = headless::LastMenu()->menu.menus[0];
  KR_EXPECT_EQ(rows[0].label, std::string("New tab"));
  KR_EXPECT_EQ(rows[0].access_key, std::string("t"));
  KR_EXPECT_EQ(rows[1].label, std::string("Q&A"));
  KR_EXPECT(rows[1].access_key.empty());
}

KR_TEST(AbiCommandBarRoundTrip) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  const KrNavState nav{.struct_size = sizeof(KrNavState),
                       .display_text = Str("example.com"),
                       .full_url = Str("https://example.com/")};
  host.ui.set_nav_state(host.ui.self, &nav);
  host.ui.focus_address(host.ui.self, true);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().command_bar_open);
  KR_EXPECT_EQ(headless::LastRender().command_text,
               std::string("https://example.com/"));

  headless::Delegate()->OnCommandTextChanged("kryp");
  KR_EXPECT_EQ(host.recorder.query, std::string("kryp"));

  const KrSuggestion items[] = {
      {.struct_size = sizeof(KrSuggestion),
       .kind = KR_SUGGESTION_SEARCH,
       .contents = Str("kryp — Search")},
      {.struct_size = sizeof(KrSuggestion),
       .kind = KR_SUGGESTION_URL,
       .flags = KR_SUGGESTION_DEFAULT,
       .contents = Str("krypton.dev")},
  };
  host.ui.set_suggestions(host.ui.self, host.recorder.query_id, items, 2,
                          sizeof(KrSuggestion));
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::LastRender().suggestions.size(), 2u);
  KR_EXPECT_EQ(headless::LastRender().selected, 1);

  headless::Delegate()->OnCommandAccepted(Disposition::kCurrentTab);
  KR_ASSERT(host.recorder.accepted.has_value());
  KR_EXPECT_EQ(host.recorder.accepted->first, host.recorder.query_id);
  KR_EXPECT_EQ(host.recorder.accepted->second, 1u);
  KR_EXPECT_EQ(host.recorder.focus_page_calls, 1);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().command_bar_open);
}

KR_TEST(AbiNewTabSubmitsTypedTextIntoNewTab) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  headless::Delegate()->OnNewTab();
  headless::Delegate()->OnCommandTextChanged("news");
  headless::Delegate()->OnCommandAccepted(Disposition::kCurrentTab);
  KR_EXPECT_EQ(host.recorder.navigated, std::string("news"));
  KR_EXPECT_EQ(host.recorder.navigated_disposition,
               static_cast<std::uint32_t>(KR_OPEN_NEW_TAB));
}

KR_TEST(AbiAcceptingWhileNewSuggestionsArePendingNavigatesTheCurrentText) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  headless::Delegate()->OnCommandBarRequested();
  headless::Delegate()->OnCommandTextChanged("old");
  const KrSuggestion item{.struct_size = sizeof(KrSuggestion),
                          .kind = KR_SUGGESTION_URL,
                          .contents = Str("old.example")};
  host.ui.set_suggestions(host.ui.self, host.recorder.query_id, &item, 1,
                          sizeof(KrSuggestion));

  headless::Delegate()->OnCommandTextChanged("new");
  headless::Delegate()->OnCommandAccepted(Disposition::kCurrentTab);
  KR_EXPECT(!host.recorder.accepted.has_value());
  KR_EXPECT_EQ(host.recorder.navigated, std::string("new"));
}

KR_TEST(AbiMenuRoundTripKeepsTheAnchor) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  headless::Delegate()->OnMenuRequested(MenuKind::kApp, 0, {10, 20, 30, 40});
  KR_EXPECT_EQ(host.recorder.menu_kind,
               static_cast<std::uint32_t>(KR_MENU_APP));

  const KrMenuItem items[] = {
      {.struct_size = sizeof(KrMenuItem),
       .command_id = 1,
       .type = KR_MENU_ITEM_COMMAND,
       .flags = KR_MENU_ITEM_ENABLED,
       .label = Str("New tab"),
       .submenu = -1},
      {.struct_size = sizeof(KrMenuItem),
       .type = KR_MENU_ITEM_SEPARATOR,
       .submenu = -1},
  };
  const KrMenu menu{sizeof(KrMenu), items, 2, sizeof(KrMenuItem)};
  host.ui.show_menu(host.ui.self, host.recorder.menu_request, &menu, 1,
                    sizeof(KrMenu));
  KR_ASSERT(headless::LastMenu().has_value());
  KR_EXPECT_EQ(headless::LastMenu()->menu.menus[0][0].label,
               std::string("New tab"));
  KR_EXPECT_EQ(headless::LastMenu()->anchor, (Rect{10, 20, 30, 40}));
}

KR_TEST(AbiRejectsMalformedMenuArraysAndSubmenuCycles) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  headless::Delegate()->OnMenuRequested(MenuKind::kApp, 0, {});
  const auto request = host.recorder.menu_request;

  host.ui.show_menu(host.ui.self, request, nullptr, 1, sizeof(KrMenu));
  const KrMenu missing_items{sizeof(KrMenu), nullptr, 1, sizeof(KrMenuItem)};
  host.ui.show_menu(host.ui.self, request, &missing_items, 1, sizeof(KrMenu));
  KR_EXPECT(!headless::LastMenu().has_value());
  KR_EXPECT_EQ(host.recorder.logs.size(), 2u);

  const KrMenuItem to_submenu{.struct_size = sizeof(KrMenuItem),
                              .type = KR_MENU_ITEM_SUBMENU,
                              .submenu = 1};
  const KrMenuItem to_root{.struct_size = sizeof(KrMenuItem),
                           .type = KR_MENU_ITEM_SUBMENU,
                           .submenu = 0};
  const KrMenu cycle[] = {{sizeof(KrMenu), &to_submenu, 1, sizeof(KrMenuItem)},
                          {sizeof(KrMenu), &to_root, 1, sizeof(KrMenuItem)}};
  host.ui.show_menu(host.ui.self, request, cycle, 2, sizeof(KrMenu));
  KR_EXPECT(!headless::LastMenu().has_value());
  KR_EXPECT_EQ(host.recorder.logs.size(), 3u);

  const KrMenuItem invalid_index{.struct_size = sizeof(KrMenuItem),
                                 .type = KR_MENU_ITEM_SUBMENU,
                                 .submenu = 5};
  const KrMenu invalid{sizeof(KrMenu), &invalid_index, 1, sizeof(KrMenuItem)};
  host.ui.show_menu(host.ui.self, request, &invalid, 1, sizeof(KrMenu));
  KR_EXPECT(!headless::LastMenu().has_value());
  KR_EXPECT_EQ(host.recorder.logs.size(), 4u);
}

KR_TEST(AbiAcceptsAnAcyclicMenuGraphWithSharedSubmenus) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  headless::Delegate()->OnMenuRequested(MenuKind::kApp, 0, {});
  const KrMenuItem root_items[] = {
      {.struct_size = sizeof(KrMenuItem),
       .type = KR_MENU_ITEM_SUBMENU,
       .submenu = 1},
      {.struct_size = sizeof(KrMenuItem),
       .type = KR_MENU_ITEM_SUBMENU,
       .submenu = 2},
  };
  const KrMenuItem shared{.struct_size = sizeof(KrMenuItem),
                          .type = KR_MENU_ITEM_SUBMENU,
                          .submenu = 2};
  const KrMenuItem command{
      .struct_size = sizeof(KrMenuItem), .command_id = 42, .submenu = -1};
  const KrMenu menus[] = {{sizeof(KrMenu), root_items, 2, sizeof(KrMenuItem)},
                          {sizeof(KrMenu), &shared, 1, sizeof(KrMenuItem)},
                          {sizeof(KrMenu), &command, 1, sizeof(KrMenuItem)}};
  host.ui.show_menu(host.ui.self, host.recorder.menu_request, menus, 3,
                    sizeof(KrMenu));
  KR_ASSERT(headless::LastMenu().has_value());
  KR_EXPECT_EQ(headless::LastMenu()->menu.menus.size(), 3u);
  KR_EXPECT(host.recorder.logs.empty());
}

KR_TEST(AbiSendsAnchorsOnlyWhenTheyChange) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  const Anchor menu{.kind = AnchorKind::kElement,
                    .id = std::to_underlying(Element::kAppMenu),
                    .bounds = {1, 2, 3, 4}};
  headless::Delegate()->OnAnchorsChanged(std::span(&menu, 1));
  headless::Delegate()->OnAnchorsChanged(std::span(&menu, 1));
  KR_EXPECT_EQ(host.recorder.anchor_calls, 1);

  headless::Delegate()->OnAnchorsChanged({});
  KR_EXPECT_EQ(host.recorder.anchor_calls, 2);
  KR_EXPECT(!host.recorder.last_anchor_visible);
}

KR_TEST(AbiAnchorCacheRetainsChangedAnchorsAndDropsRemovedOnesOnce) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  const Anchor initial[] = {
      {.kind = AnchorKind::kTab, .id = 1, .bounds = {1, 2, 3, 4}},
      {.kind = AnchorKind::kElement, .id = 1, .bounds = {5, 6, 7, 8}},
  };
  headless::Delegate()->OnAnchorsChanged(initial);
  KR_EXPECT_EQ(host.recorder.anchor_calls, 2);

  const Anchor moved{
      .kind = AnchorKind::kTab, .id = 1, .bounds = {10, 20, 30, 40}};
  headless::Delegate()->OnAnchorsChanged(std::span(&moved, 1));
  KR_EXPECT_EQ(host.recorder.anchor_calls, 4);  // one move, one removal
  headless::Delegate()->OnAnchorsChanged(std::span(&moved, 1));
  KR_EXPECT_EQ(host.recorder.anchor_calls, 4);

  headless::Delegate()->OnAnchorsChanged({});
  KR_EXPECT_EQ(host.recorder.anchor_calls, 5);
  headless::Delegate()->OnAnchorsChanged({});
  KR_EXPECT_EQ(host.recorder.anchor_calls, 5);
}

KR_TEST(AbiWorksWithAnOlderSmallerHostTable) {
  // A host built before set_anchor existed: everything from set_anchor on is
  // missing. The module must not call past the end of the table.
  Host host(offsetof(KrBrowser, set_anchor));
  KR_ASSERT(host.created == KR_OK);
  host.Window(1400, 900);
  const Anchor anchor{.kind = AnchorKind::kElement, .id = 1};
  headless::Delegate()->OnAnchorsChanged(std::span(&anchor, 1));
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.recorder.anchor_calls, 0);
  KR_EXPECT(host.recorder.drag_regions.empty());
  KR_EXPECT_EQ(host.recorder.content_bounds_calls, 1);  // still in range
}

KR_TEST(AbiIgnoresAnIncompleteCallbackSlot) {
  Host host(offsetof(KrBrowser, set_anchor) +
            sizeof(decltype(KrBrowser::set_anchor)) - 1);
  KR_ASSERT(host.created == KR_OK);
  const Anchor anchor{.kind = AnchorKind::kElement, .id = 1};
  headless::Delegate()->OnAnchorsChanged(std::span(&anchor, 1));
  KR_EXPECT_EQ(host.recorder.anchor_calls, 0);
}

KR_TEST(AbiHostMistakesAreLoggedNotFatal) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  const KrTab ghost{
      .struct_size = sizeof(KrTab), .id = 404, .title = Str("ghost")};
  host.ui.tab_updated(host.ui.self, &ghost);
  host.ui.tab_removed(host.ui.self, 404);
  KR_EXPECT_EQ(host.recorder.logs.size(), 2u);
}

}  // namespace
}  // namespace kr

namespace kr {
KR_TEST(AbiHorizontalLayoutsRoundTripAndDynamicCountUpdatesContentBounds) {
  Host host;
  KR_ASSERT(host.created == KR_OK);
  host.Window(1200, 800);
  host.Tab(0, 1, "first");
  for (const auto flag : {KR_LAYOUT_TABS_CLASSIC, KR_LAYOUT_TABS_COMPACT,
                          KR_LAYOUT_TABS_DYNAMIC}) {
    const KrLayoutPrefs prefs{.struct_size = sizeof(KrLayoutPrefs),
                              .flags = static_cast<std::uint32_t>(flag)};
    host.ui.set_layout_prefs(host.ui.self, &prefs);
    headless::RunPendingTasks();
    KR_EXPECT(!headless::LastRender().layout.sidebar_visible);
    KR_EXPECT_EQ(headless::LastRender().horizontal_tabs.size(), 1u);
  }
  KR_EXPECT(!headless::LastRender().layout.tab_strip_visible);
  const auto one_tab = host.recorder.content_bounds;
  host.Tab(1, 2, "second");
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().layout.tab_strip_visible);
  KR_EXPECT(host.recorder.content_bounds.y > one_tab.y);
  host.ui.tab_removed(host.ui.self, 2);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().layout.tab_strip_visible);
  KR_EXPECT_EQ(host.recorder.content_bounds.y, one_tab.y);
}
}  // namespace kr
