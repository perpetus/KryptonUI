#include "krypton/ui/presentation/tab_preview.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton/ui/presentation/presenter.hpp"
#include "krypton/ui/shell/browser_port.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {

struct PreviewHost {
  KrShellUI ui{};
  std::uint32_t requested = 0;
  KrTabId requested_tab = 0;
  std::vector<std::uint32_t> cancelled;
  std::uint32_t mute_calls = 0, mute_mask = 0, mute_values = 0;
  KrTabId mute_tab = 0;
  int activations = 0;

  [[nodiscard]] KrBrowser Table() {
    KrBrowser table{};
    table.struct_size = sizeof(table);
    table.self = this;
    table.request_tab_preview = [](void* self, std::uint32_t request,
                                   KrTabId tab) {
      auto& host = *static_cast<PreviewHost*>(self);
      host.requested = request;
      host.requested_tab = tab;
    };
    table.cancel_tab_preview = [](void* self, std::uint32_t request) {
      static_cast<PreviewHost*>(self)->cancelled.push_back(request);
    };
    table.set_tab_flags = [](void* self, KrTabId tab, std::uint32_t mask,
                             std::uint32_t values) {
      auto& host = *static_cast<PreviewHost*>(self);
      ++host.mute_calls;
      host.mute_tab = tab;
      host.mute_mask = mask;
      host.mute_values = values;
    };
    table.activate_tab = [](void* self, KrTabId) {
      ++static_cast<PreviewHost*>(self)->activations;
    };
    return table;
  }

  [[nodiscard]] KrResult Create() {
    headless::Reset();
    const auto table = Table();
    const KrCreateParams params{sizeof(KrCreateParams), KR_UI_ABI_VERSION,
                                nullptr, &table};
    ui.struct_size = sizeof(ui);
    return kr_ui_create(&params, &ui);
  }
  void Insert(KrTabId id, std::uint32_t flags = 0) {
    constexpr std::string_view url = "https://user:secret@example.com/path";
    const KrTab tab{.struct_size = sizeof(KrTab),
                    .id = id,
                    .flags = flags,
                    .title = {"Page", 4},
                    .url = {url.data(), url.size()}};
    ui.tab_inserted(ui.self, static_cast<std::size_t>(id - 1), &tab);
  }
  ~PreviewHost() {
    if (ui.self)
      ui.destroy(ui.self);
    headless::Reset();
  }
};

KR_TEST(TabPreviewWaitsLongerForWiderTabs) {
  const auto& h = design::kTokens.horizontal_tabs;
  const auto& t = design::kTokens.tab_hover;
  const auto delay = [&h](float width) {
    return TabPreviewShowDelay(width, h.tab_pinned_width, h.tab_standard_width)
        .count();
  };
  KR_EXPECT_EQ(delay(h.tab_pinned_width), std::lround(t.show_delay_minimum_ms));
  KR_EXPECT_EQ(delay(0), std::lround(t.show_delay_minimum_ms));
  const auto middle = delay(100);
  KR_EXPECT(middle > delay(60));
  KR_EXPECT(middle < std::lround(t.show_delay_maximum_ms));
  KR_EXPECT_EQ(
      delay(h.tab_standard_width),
      std::lround(t.show_delay_maximum_ms + t.show_delay_standard_extra_ms));
}

KR_TEST(TabPreviewIgnoresStaleRepliesAndDismissals) {
  PreviewHost host;
  KR_ASSERT(host.Create() == KR_OK);
  host.Insert(1);
  host.Insert(2);
  auto& delegate = *headless::Delegate();
  const auto first = delegate.OnTabPreviewRequested(TabId{1});
  const auto second = delegate.OnTabPreviewRequested(TabId{2});
  KR_EXPECT(first != 0 && second != 0 && first != second);
  KR_EXPECT_EQ(host.cancelled.size(), 1u);
  KR_EXPECT_EQ(host.cancelled.front(), first);
  delegate.OnTabPreviewDismissed(first);
  KR_EXPECT_EQ(host.cancelled.size(), 1u);
  KrTabPreview reply{sizeof(KrTabPreview), 1, 55, 1'000'000,
                     KR_TAB_PREVIEW_MEMORY_KNOWN};
  KR_EXPECT(host.ui.set_tab_preview(host.ui.self, first, &reply) == KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().preview_tab == TabId{2});
  KR_EXPECT(!headless::LastRender().preview_memory_known);
  reply.tab = 2;
  KR_EXPECT(host.ui.set_tab_preview(host.ui.self, second, &reply) == KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().preview_thumbnail == ImageId{55});
  KR_EXPECT(headless::LastRender().preview_memory_value == "1.0 MB");
  KR_EXPECT(headless::LastRender().preview_memory_text ==
            "Memory usage: 1.0 MB");
  host.ui.tab_removed(host.ui.self, 2);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().preview_tab);
  KR_EXPECT_EQ(host.cancelled.back(), second);
  KR_EXPECT_EQ(delegate.OnTabPreviewRequested(TabId{999}), 0u);
}

KR_TEST(TabPreviewRejectsMalformedPayloadAndCancelsAtShutdown) {
  PreviewHost host;
  KR_ASSERT(host.Create() == KR_OK);
  host.Insert(1);
  const auto request = headless::Delegate()->OnTabPreviewRequested(TabId{1});
  KR_EXPECT(host.ui.set_tab_preview(host.ui.self, request, nullptr) ==
            KR_ERR_INVALID_ARGUMENT);
  KrTabPreview reply{};
  KR_EXPECT(host.ui.set_tab_preview(host.ui.self, request, &reply) ==
            KR_ERR_INVALID_ARGUMENT);
  reply.struct_size = offsetof(KrTabPreview, flags) + sizeof(reply.flags);
  KR_EXPECT(host.ui.set_tab_preview(host.ui.self, request, &reply) ==
            KR_ERR_INVALID_ARGUMENT);
  reply.tab = 1;
  KR_EXPECT(host.ui.set_tab_preview(host.ui.self, request, &reply) == KR_OK);
  host.ui.destroy(host.ui.self);
  host.ui = {};
  KR_EXPECT_EQ(host.cancelled.back(), request);
}

KR_TEST(TabPreviewCopiesBrowserLabelsAndPreservesDeliberatelyEmptyFields) {
  PreviewHost host;
  KR_ASSERT(host.Create() == KR_OK);
  host.Insert(1, KR_TAB_AUDIBLE);
  const auto request = headless::Delegate()->OnTabPreviewRequested(TabId{1});
  std::string title = "Browser title", domain = "Browser domain",
              memory = "High memory usage: 1.2 GB", status = "Playing audio";
  const auto borrow = [](const std::string& text) -> KrStr {
    return {text.data(), text.size()};
  };
  KrTabPreview reply{.struct_size = sizeof(reply),
                     .tab = 1,
                     .flags = KR_TAB_PREVIEW_LABELS_PROVIDED,
                     .title = borrow(title),
                     .domain = borrow(domain),
                     .memory_text = borrow(memory),
                     .status = borrow(status)};
  KR_ASSERT(host.ui.set_tab_preview(host.ui.self, request, &reply) == KR_OK);
  title.assign("changed");
  domain.assign("changed");
  memory.assign("changed");
  status.assign("changed");
  headless::RunPendingTasks();
  const auto& rendered = headless::LastRender();
  KR_EXPECT(rendered.preview_title == "Browser title");
  KR_EXPECT(rendered.preview_domain == "Browser domain");
  KR_EXPECT(rendered.preview_memory_text == "High memory usage: 1.2 GB");
  KR_EXPECT(rendered.preview_status == "Playing audio");

  reply.title = reply.domain = reply.memory_text = reply.status = {};
  KR_ASSERT(host.ui.set_tab_preview(host.ui.self, request, &reply) == KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().preview_title.empty());
  KR_EXPECT(headless::LastRender().preview_domain.empty());
  KR_EXPECT(headless::LastRender().preview_memory_text.empty());
  KR_EXPECT(headless::LastRender().preview_status.empty());
}

KR_TEST(TabPreviewBrowserLabelsRequireCompleteValidPayloads) {
  PreviewHost host;
  KR_ASSERT(host.Create() == KR_OK);
  host.Insert(1);
  const auto request = headless::Delegate()->OnTabPreviewRequested(TabId{1});
  KrTabPreview reply{.struct_size = offsetof(KrTabPreview, title),
                     .tab = 1,
                     .flags = KR_TAB_PREVIEW_LABELS_PROVIDED};
  KR_EXPECT_EQ(host.ui.set_tab_preview(host.ui.self, request, &reply),
               KR_ERR_INVALID_ARGUMENT);
  reply.flags = 0;
  KR_EXPECT_EQ(host.ui.set_tab_preview(host.ui.self, request, &reply), KR_OK);
  reply.struct_size = sizeof(reply);
  reply.flags = KR_TAB_PREVIEW_LABELS_PROVIDED;
  for (auto* text :
       {&reply.title, &reply.domain, &reply.memory_text, &reply.status}) {
    *text = {nullptr, 1};
    KR_EXPECT_EQ(host.ui.set_tab_preview(host.ui.self, request, &reply),
                 KR_ERR_INVALID_ARGUMENT);
    *text = {};
  }
}

KR_TEST(TabPreviewBrowserPresentationHonorsImageAndMemoryVisibility) {
  Store store;
  KR_ASSERT(store.InsertTab(0, {.id = TabId{1}}));
  store.BeginTabPreview(TabId{1}, 1);
  TabPreviewData data{
      .thumbnail = ImageId{42},
      .flags = TabPreviewFlag::kLabelsProvided | TabPreviewFlag::kNoImage,
      .title = "Browser title",
      .memory_text = "Memory usage: 42 MB",
      .status = "Sharing screen"};
  KR_ASSERT(store.SetTabPreview(1, TabId{1}, data));
  Presenter presenter;
  const auto& shown = presenter.Present(store, {}, Dirty::kAll).tab_preview;
  KR_EXPECT(!shown.show_image);
  KR_EXPECT(!shown.thumbnail);
  KR_EXPECT(shown.memory_text == "Memory usage: 42 MB");
  KR_ASSERT(
      store.SetAppearancePrefs({.flags = AppearanceFlag::kHideHoverMemory}));
  const auto& hidden = presenter.Present(store, {}, Dirty::kAll).tab_preview;
  KR_EXPECT(hidden.memory_text.empty());
  KR_EXPECT(hidden.status == "Sharing screen");
}

KR_TEST(TabPreviewColorsFollowWorkspaceAndColorScheme) {
  ThemeVm theme{.palette = &design::kTokens.dark,
                .dark = true,
                .gradient_start = Color{0xffff0000}};
  const auto red = ResolveTabPreviewColors(theme);
  theme.gradient_start = Color{0xff0000ff};
  const auto blue = ResolveTabPreviewColors(theme);
  KR_EXPECT(red.background != blue.background);
  KR_EXPECT(red.background.r() > red.background.b());
  KR_EXPECT(blue.background.b() > blue.background.r());
  theme.dark = false;
  theme.palette = &design::kTokens.light;
  const auto light = ResolveTabPreviewColors(theme);
  KR_EXPECT(light.background.r() > blue.background.r());
  KR_EXPECT(light.thumbnail != blue.thumbnail);
  theme.high_contrast = true;
  KR_EXPECT_EQ(ResolveTabPreviewColors(theme).background,
               Color{theme.palette->gradient_base});
}

KR_TEST(TabPreviewSlotsRespectTruncatedHostAndOutputCapacity) {
  PreviewHost host;
  auto table = host.Table();
  table.struct_size = offsetof(KrBrowser, request_tab_preview) + 1;
  BrowserPort incomplete(table);
  incomplete.RequestTabPreview(1, TabId{1});
  incomplete.CancelTabPreview(1);
  KR_EXPECT_EQ(host.requested, 0u);
  KR_EXPECT(host.cancelled.empty());
  table.struct_size = offsetof(KrBrowser, cancel_tab_preview);
  BrowserPort request_only(table);
  request_only.RequestTabPreview(2, TabId{3});
  request_only.CancelTabPreview(2);
  KR_EXPECT_EQ(host.requested, 2u);
  KR_EXPECT_EQ(host.requested_tab, 3u);
  KR_EXPECT(host.cancelled.empty());

  headless::Reset();
  table.struct_size = sizeof(table);
  KrShellUI output{};
  output.struct_size = offsetof(KrShellUI, set_tab_preview);
  const auto sentinel =
      +[](void*, std::uint32_t, const KrTabPreview*) -> KrResult {
    return KR_ERR_PLATFORM;
  };
  output.set_tab_preview = sentinel;
  const KrCreateParams params{sizeof(KrCreateParams), KR_UI_ABI_VERSION,
                              nullptr, &table};
  KR_ASSERT(kr_ui_create(&params, &output) == KR_OK);
  KR_EXPECT(output.set_tab_preview == sentinel);
  KR_EXPECT(!KR_HAS_SLOT(&output, KrShellUI, set_tab_preview));
  output.destroy(output.self);
}

KR_TEST(TabMuteRequestsHostUpdateWithoutActivatingOrOptimisticState) {
  PreviewHost host;
  KR_ASSERT(host.Create() == KR_OK);
  host.Insert(1, KR_TAB_AUDIBLE);
  auto& delegate = *headless::Delegate();
  delegate.OnTabMuteToggled(TabId{1});
  KR_EXPECT_EQ(host.mute_tab, 1u);
  KR_EXPECT_EQ(host.mute_mask, std::uint32_t{KR_TAB_MUTED});
  KR_EXPECT_EQ(host.mute_values, std::uint32_t{KR_TAB_MUTED});
  delegate.OnTabMuteToggled(TabId{1});
  KR_EXPECT_EQ(host.mute_values, std::uint32_t{KR_TAB_MUTED});
  const KrTab muted{.struct_size = sizeof(KrTab),
                    .id = 1,
                    .flags = KR_TAB_AUDIBLE | KR_TAB_MUTED};
  host.ui.tab_updated(host.ui.self, &muted);
  delegate.OnTabMuteToggled(TabId{1});
  KR_EXPECT_EQ(host.mute_values, 0u);
  delegate.OnTabMuteToggled(TabId{999});
  KR_EXPECT_EQ(host.mute_calls, 3u);
  KR_EXPECT_EQ(host.activations, 0);
}

KR_TEST(TabPreviewMemoryDistinguishesUnknownZeroAndEstimatedValues) {
  Store store;
  KR_ASSERT(
      store.InsertTab(0, {.id = TabId{1},
                          .flags = TabFlag::kMuted,
                          .title = "Page",
                          .url = "https://user:secret@example.com/path"}));
  store.BeginTabPreview(TabId{1}, 1);
  Presenter presenter;
  const auto& initial = presenter.Present(store, {}, Dirty::kAll).tab_preview;
  KR_EXPECT(!initial.memory_known);
  KR_EXPECT(initial.memory_value == "Memory usage unavailable");
  KR_EXPECT(initial.memory_text == "Memory usage unavailable");
  KR_EXPECT(initial.domain == "example.com");
  TabPreviewData data{.flags = TabPreviewFlag::kMemoryKnown};
  KR_ASSERT(store.SetTabPreview(1, TabId{1}, data));
  KR_EXPECT(presenter.Present(store, {}, Dirty::kTabPreview)
                .tab_preview.memory_value == "0 B");
  data.memory_usage_bytes = std::numeric_limits<std::uint64_t>::max();
  data.flags = TabPreviewFlag::kMemoryKnown | TabPreviewFlag::kMemoryEstimated |
               TabPreviewFlag::kSample | TabPreviewFlag::kSleeping;
  KR_ASSERT(store.SetTabPreview(1, TabId{1}, data));
  const auto& large =
      presenter.Present(store, {}, Dirty::kTabPreview).tab_preview;
  KR_EXPECT(large.memory_value == "18.4 EB");
  KR_EXPECT(large.memory_label == "Estimated memory usage");
  KR_EXPECT(large.memory_text == "Estimated memory usage: 18.4 EB");
  KR_EXPECT(large.status == "Sample data · Sleeping · Audio muted");
  KR_ASSERT(store.SetUiText("memory_exabytes", "translated unit"));
  KR_EXPECT(
      presenter.Present(store, {}, Dirty::kText).tab_preview.memory_value ==
      "18.4 translated unit");
  KR_ASSERT(store.RemoveTab(TabId{1}));
  KR_EXPECT(!store.ui().tab_preview.tab);
}

KR_TEST(TabPreviewDomainOmitsCredentialsAndRetainsOpaqueUrls) {
  KR_EXPECT(TabPreviewDomain("https://name:password@example.com:8443/a?q#b") ==
            "example.com:8443");
  KR_EXPECT(TabPreviewDomain("https://[::1]:443/?q") == "[::1]:443");
  KR_EXPECT(TabPreviewDomain("about:blank") == "about:blank");
  KR_EXPECT(TabPreviewDomain("").empty());
}

KR_TEST(TabPreviewMetricsFollowClientWidthAndCustomTokens) {
  auto tokens = design::kTokens;
  for (const float width : {-1.0f, 0.0f, 120.0f, 300.0f, 1200.0f}) {
    const auto metrics = MeasureTabPreview(width, tokens);
    KR_EXPECT(metrics.width >= 0);
    KR_EXPECT(metrics.width <= tokens.tab_hover.width);
    KR_EXPECT(metrics.width <= std::max(0.0f, width));
    KR_EXPECT(metrics.thumbnail_height >= 0);
    if (width > 2 * tokens.tab_hover.border_width)
      KR_EXPECT_EQ(metrics.thumbnail_height,
                   (metrics.width - 2 * tokens.tab_hover.border_width) /
                       tokens.tab_hover.thumbnail_aspect_ratio);
  }
  const auto original = MeasureTabPreview(1200, tokens);
  tokens.tab_hover.width *= 2;
  const auto custom = MeasureTabPreview(1200, tokens);
  KR_EXPECT_EQ(custom.width, original.width * 2);
  KR_EXPECT(custom.thumbnail_height > original.thumbnail_height);
  tokens.tab_hover.thumbnail_aspect_ratio = 0;
  KR_EXPECT_EQ(MeasureTabPreview(1200, tokens).thumbnail_height, 0.0f);
}

KR_TEST(TabPreviewPlacementStaysOutsideBothSidebarEdges) {
  const Rect client{0, 0, 1000, 700};
  const Rect tab{8, 220, 170, 36};
  const Rect left_sidebar{0, 0, 186, 700};
  const auto left = PlaceTabPreview(client, left_sidebar, tab, 260, false);
  KR_EXPECT(left.x >=
            left_sidebar.right() + design::kTokens.tab_hover.placement_gap);
  KR_EXPECT(left.right() <= client.right());
  KR_EXPECT_EQ(left.y, tab.y);

  const Rect right_sidebar{814, 0, 186, 700};
  const Rect right_tab{822, 220, 170, 36};
  const auto right =
      PlaceTabPreview(client, right_sidebar, right_tab, 260, true);
  KR_EXPECT(right.right() <=
            right_sidebar.x - design::kTokens.tab_hover.placement_gap);
  KR_EXPECT(right.x >= client.x);
  KR_EXPECT_EQ(right.y, right_tab.y);
}

KR_TEST(TabPreviewPlacementAdaptsToCollapsedCompactAndNarrowLayouts) {
  const Rect client{30, 40, 420, 260};
  for (const float sidebar_width : {60.0f, 186.0f, 310.0f}) {
    const Rect sidebar{client.x, client.y, sidebar_width, client.height};
    const Rect tab{client.x, client.bottom() - 20, sidebar_width, 20};
    const auto frame = PlaceTabPreview(client, sidebar, tab, 500, false);
    KR_EXPECT(frame.x >=
              sidebar.right() + design::kTokens.tab_hover.placement_gap);
    KR_EXPECT(frame.right() <= client.right());
    KR_EXPECT_EQ(frame.y, client.y);
    KR_EXPECT_EQ(frame.height, client.height);
    KR_EXPECT(frame.width <= design::kTokens.tab_hover.width);
  }
  const Rect covered_sidebar = client;
  KR_EXPECT(
      PlaceTabPreview(client, covered_sidebar, client, 200, false).empty());
  KR_EXPECT(PlaceTabPreview({}, covered_sidebar, client, 200, false).empty());
}

}  // namespace
}  // namespace kr

namespace kr {
KR_TEST(HorizontalTabPreviewFitsClientAndOpensBelowTopChromeInBothDirections) {
  const Rect client{11, 19, 1000, 700};
  const Rect chrome{11, 19, 1000, 80};
  for (const bool rtl : {false, true}) {
    const auto preview =
        PlaceHorizontalTabPreview(client, chrome, {850, 59, 160, 36}, 300, rtl);
    KR_EXPECT(preview.x >= client.x && preview.right() <= client.right());
    KR_EXPECT(preview.y >= chrome.bottom());
    KR_EXPECT(preview.bottom() <= client.bottom());
  }
  const auto tiny = PlaceHorizontalTabPreview({0, 0, 100, 90}, {0, 0, 100, 40},
                                              {0, 0, 50, 36}, 300, false);
  KR_EXPECT(tiny.width <= 100 && tiny.bottom() <= 90);
}
}  // namespace kr

namespace kr {
KR_TEST(HorizontalPreviewNeverFlipsOverTheStripInShortWindows) {
  const Rect client{20, 30, 500, 130};
  const Rect chrome{20, 30, 500, 90};
  const Rect tab{180, 80, 130, 36};
  for (const bool rtl : {false, true}) {
    const auto preview =
        PlaceHorizontalTabPreview(client, chrome, tab, 2000, rtl);
    KR_EXPECT(!preview.empty());
    KR_EXPECT(preview.y >= chrome.bottom());
    KR_EXPECT(preview.bottom() <=
              client.bottom() - design::kTokens.spacing.window_margin);
    KR_EXPECT(preview.x >= client.x && preview.right() <= client.right());
  }
  KR_EXPECT(
      PlaceHorizontalTabPreview(client, client, tab, 2000, false).empty());
}

KR_TEST(HorizontalHoverPlacementIgnoresRememberedVerticalSidebarSide) {
  for (const auto mode :
       {TabLayout::kClassic, TabLayout::kCompact, TabLayout::kDynamic}) {
    ShellLayout layout;
    layout.tabs = mode;
    for (const bool sidebar_right : {false, true}) {
      layout.sidebar_on_right = sidebar_right;
      KR_EXPECT(TabPreviewPlacementFor(layout) == TabPreviewPlacement::kBelow);
    }
  }
  ShellLayout vertical;
  KR_EXPECT(TabPreviewPlacementFor(vertical) == TabPreviewPlacement::kRight);
  vertical.sidebar_on_right = true;
  KR_EXPECT(TabPreviewPlacementFor(vertical) == TabPreviewPlacement::kLeft);
}

KR_TEST(HorizontalHoverMetricsUseTheSameViewportAsTheVisibleCard) {
  const Rect client{17, 23, 210, 180};
  for (const auto mode :
       {TabLayout::kClassic, TabLayout::kCompact, TabLayout::kDynamic}) {
    ShellLayout layout;
    layout.tabs = mode;
    layout.toolbar = {17, 23, 210, 80};
    const auto metrics = MeasureTabPreviewForLayout(client, layout);
    const auto frame = PlaceHorizontalTabPreview(client, layout.toolbar,
                                                 {40, 60, 120, 36}, 2000, true);
    KR_EXPECT_EQ(metrics.width, frame.width);
    KR_EXPECT(metrics.width <=
              client.width - 2 * design::kTokens.spacing.window_margin);
  }
}

KR_TEST(HorizontalHoverFollowsTabAlignmentAndCustomGapWithoutEnteringChrome) {
  auto tokens = design::kTokens;
  tokens.tab_hover.placement_gap = 11;
  const Rect client{10, 20, 1200, 800};
  const Rect chrome{10, 20, 1200, 80};
  const Rect tab{500, 50, 100, 36};
  const auto left =
      PlaceHorizontalTabPreview(client, chrome, tab, 300, false, tokens);
  const auto right =
      PlaceHorizontalTabPreview(client, chrome, tab, 300, true, tokens);
  KR_EXPECT_EQ(left.x, tab.x);
  KR_EXPECT_EQ(right.right(), tab.right());
  KR_EXPECT_EQ(left.y, chrome.bottom() + tokens.tab_hover.placement_gap);
  KR_EXPECT_EQ(left.y, right.y);
}

KR_TEST(TabLayoutChangesCancelPreviewWorkAndRejectLateReplies) {
  PreviewHost host;
  KR_ASSERT(host.Create() == KR_OK);
  host.Insert(1);
  for (const auto flag : std::array<std::uint32_t, 4>{
           KR_LAYOUT_TABS_CLASSIC, KR_LAYOUT_TABS_COMPACT,
           KR_LAYOUT_TABS_DYNAMIC, 0}) {
    const auto request = headless::Delegate()->OnTabPreviewRequested(TabId{1});
    KR_ASSERT(request != 0);
    const auto cancelled = host.cancelled.size();
    const KrLayoutPrefs prefs{.struct_size = sizeof(KrLayoutPrefs),
                              .flags = static_cast<std::uint32_t>(flag)};
    host.ui.set_layout_prefs(host.ui.self, &prefs);
    headless::RunPendingTasks();
    KR_EXPECT_EQ(host.cancelled.size(), cancelled + 1);
    KR_EXPECT_EQ(host.cancelled.back(), request);
    const KrTabPreview late{sizeof(KrTabPreview), 1, 55, 1000,
                            KR_TAB_PREVIEW_MEMORY_KNOWN};
    KR_EXPECT_EQ(host.ui.set_tab_preview(host.ui.self, request, &late), KR_OK);
    headless::RunPendingTasks();
    KR_EXPECT(!headless::LastRender().preview_tab);
  }
}
}  // namespace kr
