#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {

struct InterfaceHost {
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};
  int bounds_calls = 0;
  KrRect bounds{};
  float radius = 0;
  bool reenter = false;
  bool consume_key = false;
  int key_calls = 0;
  const void* last_key = nullptr;
  std::uint32_t query = 0;
  std::uint32_t menu = 0;

  explicit InterfaceHost(std::size_t browser_size = sizeof(KrBrowser)) {
    headless::Reset();
    KrBrowser browser{.struct_size = browser_size, .self = this};
    browser.set_content_bounds = [](void* self, KrRect rect, float corner) {
      auto& host = *static_cast<InterfaceHost*>(self);
      ++host.bounds_calls;
      host.bounds = rect;
      host.radius = corner;
      if (host.reenter) {
        host.reenter = false;
        host.Window(900, 600);
      }
    };
    browser.unhandled_key = [](void* self, const void* key) {
      auto& host = *static_cast<InterfaceHost*>(self);
      ++host.key_calls;
      host.last_key = key;
      return host.consume_key;
    };
    browser.query_suggestions = [](void* self, std::uint32_t id, KrStr) {
      static_cast<InterfaceHost*>(self)->query = id;
    };
    browser.request_menu = [](void* self, std::uint32_t id, std::uint32_t,
                              std::uint64_t, KrRect) {
      static_cast<InterfaceHost*>(self)->menu = id;
    };
    const KrCreateParams params{sizeof(KrCreateParams), KR_UI_ABI_VERSION,
                                nullptr, &browser};
    KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
    headless::RunPendingTasks();
    bounds_calls = 0;
  }
  ~InterfaceHost() {
    ui.destroy(ui.self);
    headless::RunPendingTasks();
  }
  InterfaceHost(const InterfaceHost&) = delete;
  InterfaceHost& operator=(const InterfaceHost&) = delete;

  void Window(float width,
              float height,
              std::uint32_t flags = KR_WINDOW_ACTIVE,
              float scale = 1) {
    const KrWindowState window{
        sizeof(KrWindowState), flags, scale, {0, 0, width, height}};
    ui.set_window_state(ui.self, &window);
  }
};

template <typename T>
struct Extended {
  T record{};
  std::uint64_t future = std::numeric_limits<std::uint64_t>::max();
  Extended() { record.struct_size = sizeof(Extended); }
};

KR_TEST(AbiV2RejectsV1BeforeCreatingAView) {
  headless::Reset();
  KrBrowser browser{.struct_size = sizeof(KrBrowser)};
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};
  const KrCreateParams params{sizeof(KrCreateParams), 1, nullptr, &browser};
  KR_EXPECT_EQ(kr_ui_create(&params, &ui), KR_ERR_VERSION);
  KR_EXPECT(!kr_ui_is_supported(1));
  KR_EXPECT(headless::Delegate() == nullptr);
  KR_EXPECT(ui.self == nullptr);
  KR_EXPECT_EQ(ui.struct_size, sizeof(KrShellUI));
}

KR_TEST(AbiV2IgnoresIncompleteStandalonePayloads) {
  InterfaceHost host;
  KrWindowState window{.struct_size = offsetof(KrWindowState, client)};
  KrTheme theme{.struct_size = offsetof(KrTheme, accent)};
  KrLayoutPrefs layout{.struct_size = offsetof(KrLayoutPrefs, sidebar_width)};
  KrTab tab{.struct_size = offsetof(KrTab, favicon), .id = 7};
  KrNavState nav{.struct_size = offsetof(KrNavState, full_url)};
  KrImage image{.struct_size = offsetof(KrImage, pixels), .id = 9};
  const auto renders = headless::RenderCount();
  host.ui.set_window_state(host.ui.self, &window);
  host.ui.set_theme(host.ui.self, &theme);
  host.ui.set_layout_prefs(host.ui.self, &layout);
  host.ui.tab_inserted(host.ui.self, 0, &tab);
  host.ui.tab_updated(host.ui.self, &tab);
  host.ui.set_nav_state(host.ui.self, &nav);
  host.ui.put_image(host.ui.self, &image);
  KR_EXPECT_EQ(host.bounds_calls, 0);
  KR_EXPECT_EQ(headless::PendingTaskCount(), 0u);
  KR_EXPECT_EQ(headless::RenderCount(), renders);
}

KR_TEST(AbiV2AcceptsExtendedStandalonePayloadsAndCopiesStrings) {
  InterfaceHost host;
  Extended<KrWindowState> window;
  window.record.client = {0, 0, 900, 600};
  window.record.scale_factor = 1.5f;
  Extended<KrTheme> theme;
  theme.record.dark = true;
  Extended<KrLayoutPrefs> layout;
  layout.record.flags = KR_LAYOUT_TOOLBAR_SINGLE;
  Extended<KrTab> tab;
  tab.record.id = 5;
  std::string title = "borrowed tab title";
  tab.record.title = {title.data(), title.size()};
  Extended<KrNavState> nav;
  nav.record.display_text = {"example.test", 12};
  nav.record.full_url = {"https://example.test/", 21};
  host.ui.set_layout_prefs(host.ui.self, &layout.record);
  host.ui.set_window_state(host.ui.self, &window.record);
  host.ui.set_theme(host.ui.self, &theme.record);
  host.ui.tab_inserted(host.ui.self, 0, &tab.record);
  host.ui.set_active_tab(host.ui.self, 5);
  host.ui.set_nav_state(host.ui.self, &nav.record);
  title.assign(title.size(), '!');
  headless::RunPendingTasks();
  KR_ASSERT(headless::LastRender().today.size() == 1);
  KR_EXPECT_EQ(headless::LastRender().today[0], "borrowed tab title");
  KR_EXPECT_EQ(headless::LastRender().address, "example.test");
  KR_EXPECT(!headless::LastRender().layout.toolbar_visible);
}

KR_TEST(AbiV2ReadsExtendedSpaceActionAndSuggestionStrides) {
  InterfaceHost host;
  std::array<Extended<KrSpace>, 2> spaces;
  spaces[0].record.id = 1;
  spaces[0].record.name = {"First", 5};
  spaces[1].record.id = 2;
  spaces[1].record.name = {"Second", 6};
  host.ui.set_spaces(host.ui.self, &spaces[0].record, spaces.size(),
                     sizeof(spaces[0]), 2);
  std::array<Extended<KrAction>, 2> actions;
  for (std::size_t i = 0; i < actions.size(); ++i) {
    actions[i].record.id = i + 1;
    actions[i].record.flags = KR_ACTION_VISIBLE | KR_ACTION_ENABLED;
    actions[i].record.kind = KR_ACTION_PAGE;
  }
  // The record size may be smaller than the array stride (trailing storage).
  actions[1].record.struct_size = sizeof(KrAction);
  host.ui.set_actions(host.ui.self, &actions[0].record, actions.size(),
                      sizeof(actions[0]));
  host.ui.focus_address(host.ui.self, false);
  headless::RunPendingTasks();
  headless::Delegate()->OnCommandTextChanged("example");
  std::array<Extended<KrSuggestion>, 2> suggestions;
  suggestions[0].record.contents = {"first.test", 10};
  suggestions[1].record.contents = {"second.test", 11};
  host.ui.set_suggestions(host.ui.self, host.query, &suggestions[0].record,
                          suggestions.size(), sizeof(suggestions[0]));
  headless::RunPendingTasks();
  const auto& view = headless::LastRender();
  KR_ASSERT(view.spaces.size() == 2);
  KR_EXPECT_EQ(view.spaces[1], "Second");
  KR_EXPECT_EQ(view.page_actions, 2u);
  KR_ASSERT(view.suggestions.size() == 2);
  KR_EXPECT_EQ(view.suggestions[1], "second.test");
}

KR_TEST(AbiV2ReadsActionBadgeTextColorOnlyWhenPresent) {
  InterfaceHost host;
  std::array<KrAction, 2> actions{{
      {.struct_size = sizeof(KrAction),
       .id = 1,
       .kind = KR_ACTION_EXTENSION,
       .flags = KR_ACTION_VISIBLE,
       .badge = {"22", 2},
       .badge_color = 0xFFFDD835,
       .progress = -1,
       .badge_text_color = 0xFF000000},
      // A host built before the field existed sends the shorter record.
      {.struct_size = offsetof(KrAction, badge_text_color),
       .id = 2,
       .kind = KR_ACTION_EXTENSION,
       .flags = KR_ACTION_VISIBLE,
       .badge = {"3", 1},
       .badge_color = 0xFF1A73E8,
       .progress = -1,
       .badge_text_color = 0xFF00FF00},
  }};
  host.ui.set_actions(host.ui.self, actions.data(), actions.size(),
                      sizeof(KrAction));
  headless::RunPendingTasks();
  const auto& badges = headless::LastRender().page_action_badges;
  KR_ASSERT(badges.size() == 2);
  KR_EXPECT_EQ(badges[0].text, "22");
  KR_EXPECT_EQ(badges[0].background.argb, 0xFFFDD835u);
  KR_EXPECT_EQ(badges[0].foreground.argb, 0xFF000000u);
  KR_EXPECT_EQ(badges[1].text, "3");
  KR_EXPECT_EQ(badges[1].foreground.argb, 0u);
}

KR_TEST(AbiV2RejectsInvalidArraysWithoutClearingExistingState) {
  InterfaceHost host;
  std::array<KrAction, 2> actions{{
      {.struct_size = sizeof(KrAction), .id = 1, .flags = KR_ACTION_VISIBLE},
      {.struct_size = sizeof(KrAction), .id = 2, .flags = KR_ACTION_VISIBLE},
  }};
  host.ui.set_actions(host.ui.self, actions.data(), 2, sizeof(KrAction));
  headless::RunPendingTasks();
  const auto renders = headless::RenderCount();
  host.ui.set_actions(host.ui.self, nullptr, 1, sizeof(KrAction));
  host.ui.set_actions(host.ui.self, actions.data(), 2, sizeof(size_t));
  host.ui.set_actions(host.ui.self, actions.data(),
                      std::numeric_limits<std::size_t>::max(),
                      sizeof(KrAction));
  actions[1].struct_size = sizeof(KrAction) + 1;
  host.ui.set_actions(host.ui.self, actions.data(), 2, sizeof(KrAction));
  actions[1].struct_size = offsetof(KrAction, progress);
  host.ui.set_actions(host.ui.self, actions.data(), 2, sizeof(KrAction));
  KR_EXPECT_EQ(headless::PendingTaskCount(), 0u);
  KR_EXPECT_EQ(headless::RenderCount(), renders);
  KR_EXPECT_EQ(headless::LastRender().page_actions, 2u);
  host.ui.set_actions(host.ui.self, nullptr, 0, 0);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::LastRender().page_actions, 0u);
}

KR_TEST(AbiV2ReadsRecordsAtUnalignedByteStrides) {
  InterfaceHost host;
  constexpr auto stride = sizeof(KrAction) + 1;
  alignas(KrAction) std::array<std::byte, 2 * stride> bytes{};
  KrAction action{
      .struct_size = sizeof(KrAction), .id = 1, .flags = KR_ACTION_VISIBLE};
  std::memcpy(bytes.data(), &action, sizeof(action));
  action.id = 2;
  std::memcpy(bytes.data() + stride, &action, sizeof(action));
  host.ui.set_actions(
      host.ui.self, reinterpret_cast<const KrAction*>(bytes.data()), 2, stride);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::LastRender().page_actions, 2u);
}

KR_TEST(AbiV2ReadsExtendedNestedMenuArraysAndPreservesGlyphs) {
  InterfaceHost host;
  host.Window(900, 600);
  headless::RunPendingTasks();
  headless::Delegate()->OnMenuRequested(MenuKind::kPage, 0, {});
  std::array<Extended<KrMenuItem>, 2> items;
  for (std::size_t i = 0; i < items.size(); ++i) {
    items[i].record.command_id = static_cast<std::int32_t>(i + 1);
    items[i].record.flags = KR_MENU_ITEM_ENABLED;
    items[i].record.submenu = -1;
  }
  items[1].record.label = {"Second", 6};
  items[1].record.glyph = {"G", 1};
  Extended<KrMenu> menu;
  menu.record.items = &items[0].record;
  menu.record.count = items.size();
  menu.record.item_size = sizeof(items[0]);
  host.ui.show_menu(host.ui.self, host.menu, &menu.record, 1, sizeof(menu));
  KR_ASSERT(headless::LastMenu());
  const auto& row = headless::LastMenu()->menu.menus[0][1];
  KR_EXPECT_EQ(row.label, "Second");
  KR_EXPECT_EQ(row.glyph, "G");
  // A malformed child record invalidates the whole reply.
  items[1].record.struct_size = offsetof(KrMenuItem, glyph);
  items[0].record.label = {"Invalid", 7};
  host.ui.show_menu(host.ui.self, host.menu, &menu.record, 1, sizeof(menu));
  KR_EXPECT(headless::LastMenu()->menu.menus[0][0].label.empty());
}

KR_TEST(AbiV2ShortcutFallbackIsOptionalBorrowedAndReturnsConsumption) {
  for (const auto capacity : {offsetof(KrBrowser, unhandled_key),
                              offsetof(KrBrowser, unhandled_key) +
                                  sizeof(KrBrowser::unhandled_key) - 1,
                              sizeof(KrBrowser)}) {
    InterfaceHost host(capacity);
    const int message = 42;
    auto* delegate = headless::Delegate();
    KR_EXPECT(!delegate->OnUnhandledKey(nullptr));
    KR_EXPECT(!delegate->OnUnhandledKey(&message));
    host.consume_key = true;
    const bool available = capacity == sizeof(KrBrowser);
    KR_EXPECT_EQ(delegate->OnUnhandledKey(&message), available);
    KR_EXPECT_EQ(host.key_calls, available ? 2 : 0);
    KR_EXPECT(host.last_key == (available ? &message : nullptr));
    KR_EXPECT_EQ(headless::PendingTaskCount(), 0u);
  }
}

KR_TEST(AbiV2ResizeReportsSynchronouslyWithoutRenderingOrDuplicateCallbacks) {
  InterfaceHost host;
  const auto renders = headless::RenderCount();
  host.reenter = true;
  host.Window(900, 600);
  KR_EXPECT_EQ(host.bounds_calls, 1);
  KR_EXPECT_EQ(headless::RenderCount(), renders);
  KR_EXPECT_EQ(headless::PendingTaskCount(), 1u);
  host.Window(900, 600);
  KR_EXPECT_EQ(host.bounds_calls, 1);
  host.Window(800, 500);
  KR_EXPECT_EQ(host.bounds_calls, 2);
  const auto bounds = host.bounds;
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::RenderCount(), renders + 1);
  KR_EXPECT_EQ(host.bounds_calls, 2);
  KR_EXPECT_EQ(headless::LastRender().layout.card.width, bounds.width);
  KR_EXPECT_EQ(headless::LastRender().layout.card.height, bounds.height);
}

KR_TEST(AbiV2ResizeUsesPendingLayoutTabCountFullscreenAndDpiState) {
  InterfaceHost host;
  const KrTab first{.struct_size = sizeof(KrTab), .id = 1};
  const KrTab second{.struct_size = sizeof(KrTab), .id = 2};
  host.ui.tab_inserted(host.ui.self, 0, &first);
  host.ui.tab_inserted(host.ui.self, 1, &second);
  for (const auto flags : {0u, static_cast<unsigned>(KR_LAYOUT_TOOLBAR_SINGLE),
                           static_cast<unsigned>(KR_LAYOUT_TABS_CLASSIC),
                           static_cast<unsigned>(KR_LAYOUT_TABS_COMPACT),
                           static_cast<unsigned>(KR_LAYOUT_TABS_DYNAMIC),
                           static_cast<unsigned>(KR_LAYOUT_COMPACT_MODE)}) {
    const KrLayoutPrefs layout{sizeof(KrLayoutPrefs), flags, 300};
    host.ui.set_layout_prefs(host.ui.self, &layout);
    for (const auto state : {static_cast<unsigned>(KR_WINDOW_ACTIVE),
                             static_cast<unsigned>(KR_WINDOW_FULLSCREEN)}) {
      host.Window(650, 420, state, 2);
      const auto before = host.bounds_calls;
      const auto bounds = host.bounds;
      const auto radius = host.radius;
      headless::RunPendingTasks();
      const auto& rendered = headless::LastRender().layout;
      KR_EXPECT_EQ(rendered.card.x, bounds.x);
      KR_EXPECT_EQ(rendered.card.y, bounds.y);
      KR_EXPECT_EQ(rendered.card.width, bounds.width);
      KR_EXPECT_EQ(rendered.card.height, bounds.height);
      KR_EXPECT_EQ(rendered.card_radius, radius);
      KR_EXPECT_EQ(host.bounds_calls, before);
    }
  }
}

KR_TEST(NativeRowsCanRequestCurrentImageDataOutsideRender) {
  InterfaceHost host;
  const std::array<std::uint8_t, 4> first{1, 2, 3, 255};
  KrImage image{sizeof(KrImage), 77, 1, 1, 4, first.data()};
  host.ui.put_image(host.ui.self, &image);
  headless::RunPendingTasks();
  const auto* initial = headless::Delegate()->OnImageRequested(ImageId{77});
  KR_ASSERT(initial);
  KR_EXPECT_EQ(initial->pixels[0], 1);
  const auto generation = initial->generation;
  const std::array<std::uint8_t, 4> replacement{4, 5, 6, 255};
  image.pixels = replacement.data();
  host.ui.put_image(host.ui.self, &image);
  const auto* current = headless::Delegate()->OnImageRequested(ImageId{77});
  KR_ASSERT(current);
  KR_EXPECT(current->generation != generation);
  KR_EXPECT_EQ(current->pixels[0], 4);
  host.ui.drop_image(host.ui.self, 77);
  KR_EXPECT(headless::Delegate()->OnImageRequested(ImageId{77}) == nullptr);
}

KR_TEST(NativeRowsRequestCurrentTabPresentationWithoutRendering) {
  InterfaceHost host;
  KrTab tab{.struct_size = sizeof(KrTab),
            .id = 42,
            .flags = KR_TAB_AUDIBLE,
            .title = {"Before", 6},
            .url = {"https://example.com", 19},
            .favicon = 7};
  host.ui.tab_inserted(host.ui.self, 0, &tab);
  host.ui.set_active_tab(host.ui.self, 42);
  const auto renders = headless::RenderCount();
  const auto first = headless::Delegate()->OnTabRequested(TabId{42});
  KR_ASSERT(first);
  KR_EXPECT_EQ(first->title, "Before");
  KR_EXPECT(first->active && first->audible && !first->muted);
  tab.title = {"After", 5};
  tab.flags = KR_TAB_MUTED | KR_TAB_LOADING;
  tab.favicon = 8;
  host.ui.tab_updated(host.ui.self, &tab);
  const auto current = headless::Delegate()->OnTabRequested(TabId{42});
  KR_ASSERT(current);
  KR_EXPECT_EQ(current->title, "After");
  KR_EXPECT_EQ(current->url, "https://example.com");
  KR_EXPECT_EQ(current->favicon, ImageId{8});
  KR_EXPECT(current->active && current->muted && !current->audible);
  KR_EXPECT_EQ(headless::RenderCount(), renders);
  host.ui.tab_removed(host.ui.self, 42);
  KR_EXPECT(!headless::Delegate()->OnTabRequested(TabId{42}));
}

KR_TEST(AbiRuntimeShutdownRejectsLiveShellsAndSucceedsAfterDestroy) {
  {
    InterfaceHost host;
    KR_EXPECT_EQ(kr_ui_shutdown(), KR_ERR_INVALID_ARGUMENT);
    host.Window(900, 600);
    headless::RunPendingTasks();
    KR_EXPECT(headless::Delegate() != nullptr);
  }
  KR_EXPECT_EQ(kr_ui_shutdown(), KR_OK);
  KR_EXPECT_EQ(kr_ui_shutdown(), KR_OK);
}

}  // namespace
}  // namespace kr
