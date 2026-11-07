#include <array>
#include <cstddef>
#include <limits>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {
struct MultiPageHost {
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};
  std::uint32_t capabilities = 0;
  std::size_t commands = 0;
  KrMultiPageCommand command{};
  KrSplitEdit edit{};
  explicit MultiPageHost(std::size_t output_size = sizeof(KrShellUI),
                         std::uint32_t caps = 0)
      : capabilities(caps) {
    headless::Reset();
    KrBrowser host{.struct_size = offsetof(KrBrowser, multi_page_capabilities)};
    if (caps) {
      host.struct_size = sizeof(host);
      host.self = this;
      host.multi_page_capabilities = [](void* self) noexcept {
        return static_cast<MultiPageHost*>(self)->capabilities;
      };
      host.multi_page_command = [](void* self,
                                   const KrMultiPageCommand* value) noexcept {
        auto& owner = *static_cast<MultiPageHost*>(self);
        ++owner.commands;
        owner.command = *value;
        if (value->edit)
          owner.edit = *value->edit;
        owner.command.edit = nullptr;
        owner.command.members = nullptr;
        return KR_OK;
      };
    }
    const KrCreateParams params{sizeof(KrCreateParams), KR_UI_ABI_VERSION,
                                nullptr, &host};
    ui.struct_size = output_size;
    KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
  }
  ~MultiPageHost() {
    ui.destroy(ui.self);
    headless::Reset();
  }
};

struct GroupPayload {
  std::array<KrSplitMember, 4> members{{{sizeof(KrSplitMember), 11},
                                        {sizeof(KrSplitMember), 12},
                                        {sizeof(KrSplitMember), 13},
                                        {sizeof(KrSplitMember), 14}}};
  std::array<KrSplitWeight, 3> weights{{{sizeof(KrSplitWeight), 0.5f},
                                        {sizeof(KrSplitWeight), 0.5f},
                                        {sizeof(KrSplitWeight), 0.5f}}};
  KrSplitGroup group{sizeof(KrSplitGroup),
                     7,
                     3,
                     1,
                     KR_SPLIT_GRID,
                     11,
                     members.data(),
                     members.size(),
                     sizeof(KrSplitMember),
                     weights.data(),
                     weights.size(),
                     sizeof(KrSplitWeight)};
};

struct GeometryPayload {
  std::array<KrPaneBounds, 4> panes{};
  std::array<KrDividerBounds, 3> dividers{};
  KrSplitGeometry geometry{
      sizeof(KrSplitGeometry), panes.data(),    panes.size(),
      sizeof(KrPaneBounds),    dividers.data(), dividers.size(),
      sizeof(KrDividerBounds)};
  GeometryPayload() {
    for (auto& pane : panes)
      pane.struct_size = sizeof(pane);
    for (auto& divider : dividers)
      divider.struct_size = sizeof(divider);
  }
};

KR_TEST(MultiPageVisualOptionsApplyOneLayoutAndRejectStaleOpenMenus) {
  MultiPageHost host(sizeof(KrShellUI),
                     KR_MULTI_PAGE_COMPLETE | KR_MULTI_PAGE_SNAP_LAYOUTS);
  for (KrTabId id = 11; id <= 14; ++id) {
    const KrTab tab{.struct_size = sizeof(KrTab), .id = id, .space = 3};
    host.ui.tab_inserted(host.ui.self, static_cast<std::size_t>(id - 11), &tab);
  }
  host.ui.set_active_tab(host.ui.self, 11);
  GroupPayload payload;
  KR_ASSERT(host.ui.upsert_split_group(host.ui.self, &payload.group) == KR_OK);
  KR_ASSERT(host.ui.show_split_options(host.ui.self, 12, {100, 80, 20, 20}) ==
            KR_OK);
  KR_ASSERT(headless::Delegate());
  headless::Delegate()->OnSplitLayoutSelected(SplitLayout::kMainLeft);
  KR_EXPECT_EQ(host.commands, 1u);
  KR_EXPECT_EQ(host.command.kind, KR_MULTI_PAGE_EDIT_SPLIT);
  KR_EXPECT_EQ(host.command.group, 7u);
  KR_EXPECT_EQ(host.edit.kind, KR_SPLIT_SET_LAYOUT);
  KR_EXPECT_EQ(host.edit.layout, KR_SPLIT_MAIN_LEFT);
  KR_EXPECT_EQ(host.edit.tab, 12u);
  KR_ASSERT(host.ui.show_split_options(host.ui.self, 12, {100, 80, 20, 20}) ==
            KR_OK);
  ++payload.group.revision;
  KR_ASSERT(host.ui.upsert_split_group(host.ui.self, &payload.group) == KR_OK);
  headless::Delegate()->OnSplitLayoutSelected(SplitLayout::kGrid);
  KR_EXPECT_EQ(host.commands, 1u);
  KR_EXPECT(host.ui.show_split_options(host.ui.self, 99, {0, 0, 1, 1}) !=
            KR_OK);
  {
    MultiPageHost legacy(offsetof(KrShellUI, show_split_options));
    KR_EXPECT(!KR_HAS_SLOT(&legacy.ui, KrShellUI, show_split_options));
    KR_EXPECT(legacy.ui.show_split_options == nullptr);
  }
}

KR_TEST(MultiPageAbiGlanceMetricsRespectRecordCapacity) {
  MultiPageHost host;
  KrMultiPageMetrics metrics{
      .struct_size = offsetof(KrMultiPageMetrics, glance_button_size)};
  metrics.glance_button_size = -42;
  KR_EXPECT(host.ui.get_multi_page_metrics(host.ui.self, &metrics) == KR_OK);
  KR_EXPECT(metrics.glance_button_size == -42);
  KR_EXPECT(metrics.outline_width > 0);
  metrics.struct_size = offsetof(KrMultiPageMetrics, glance_button_gap);
  metrics.glance_button_gap = -42;
  KR_EXPECT(host.ui.get_multi_page_metrics(host.ui.self, &metrics) == KR_OK);
  KR_EXPECT(metrics.glance_button_size == 34 &&
            metrics.glance_button_gap == -42);
  metrics.struct_size = sizeof(metrics);
  KR_EXPECT(host.ui.get_multi_page_metrics(host.ui.self, &metrics) == KR_OK);
  KR_EXPECT(metrics.glance_button_gap == 12 && metrics.glance_icon_size == 18);
  KR_EXPECT(metrics.glance_parent_scale == 0.97f &&
            metrics.glance_parent_opacity == 0.3f);
  KR_EXPECT(metrics.glance_motion_ms == 350 &&
            metrics.glance_controls_ms == 200);
  KR_EXPECT(metrics.glance_close_speed == 1.5f);
  KR_EXPECT(metrics.glance_genie_source_scale_x == 0.04f);
  KR_EXPECT(metrics.glance_genie_neck_scale_x >
            metrics.glance_genie_source_scale_x);
  KrGlanceGeometry geometry{.struct_size = sizeof(KrGlanceGeometry)};
  KR_EXPECT(host.ui.layout_glance_card(host.ui.self, {100, 50, 1000, 600},
                                       &geometry) == KR_OK);
  KR_EXPECT(geometry.page.x == 200 && geometry.page.width == 800);
  const KrLayoutPrefs right{sizeof(KrLayoutPrefs), KR_LAYOUT_SIDEBAR_RIGHT,
                            280};
  host.ui.set_layout_prefs(host.ui.self, &right);
  KR_EXPECT(host.ui.layout_glance_card(host.ui.self, {100, 50, 1000, 600},
                                       &geometry) == KR_OK);
  KR_EXPECT(geometry.toolbar.x + geometry.toolbar.width == geometry.page.x);
  KR_EXPECT(host.ui.get_multi_page_metrics(host.ui.self, &metrics) == KR_OK);
  KR_EXPECT(metrics.glance_controls_on_left);
  const KrLayoutPrefs classic{sizeof(KrLayoutPrefs),
                              KR_LAYOUT_SIDEBAR_RIGHT | KR_LAYOUT_TABS_CLASSIC,
                              280};
  host.ui.set_layout_prefs(host.ui.self, &classic);
  KR_EXPECT(host.ui.get_multi_page_metrics(host.ui.self, &metrics) == KR_OK);
  KR_EXPECT(!metrics.glance_controls_on_left);
}

KR_TEST(MultiPageAbiOldHostsGetStatelessSupport) {
  MultiPageHost host;
  const auto caps = host.ui.multi_page_capabilities(host.ui.self);
  KR_EXPECT((caps & KR_MULTI_PAGE_LAYOUT) != 0);
  KR_EXPECT((caps & KR_MULTI_PAGE_REDUCERS) != 0);
  GroupPayload input;
  GeometryPayload output;
  const KrSplitLayoutRequest request{sizeof(KrSplitLayoutRequest),
                                     &input.group,
                                     {10, 20, 1000, 800},
                                     4,
                                     false};
  KR_EXPECT(host.ui.layout_split(host.ui.self, &request, &output.geometry) ==
            KR_OK);
  KR_EXPECT(output.geometry.pane_count == 4);
  KR_EXPECT(output.panes[0].tab == 11 && output.panes[3].tab == 14);
  KR_EXPECT(output.panes[0].bounds.x == 10);
  KR_EXPECT(output.dividers[0].index == 0 && !output.dividers[0].horizontal);
  KR_EXPECT(output.dividers[1].horizontal);
}
KR_TEST(MultiPageAbiSnapChooserRequiresHostCapabilityAndAnAppendedSlot) {
  for (const bool snaps : {false, true}) {
    MultiPageHost host(
        sizeof(KrShellUI),
        KR_MULTI_PAGE_COMPLETE | (snaps ? KR_MULTI_PAGE_SNAP_LAYOUTS : 0));
    const KrTab tab{.struct_size = sizeof(KrTab), .id = 11, .space = 3};
    host.ui.tab_inserted(host.ui.self, 0, &tab);
    host.ui.set_active_tab(host.ui.self, tab.id);
    KR_EXPECT(host.ui.show_split_chooser_for_layout(host.ui.self,
                                                    KR_SPLIT_MAIN_LEFT) ==
              (snaps ? KR_OK : KR_ERR_UNSUPPORTED));
    KR_EXPECT(host.ui.show_split_chooser_for_layout(
                  host.ui.self, static_cast<KrSplitLayout>(100)) ==
              KR_ERR_UNSUPPORTED);
    KR_EXPECT(host.ui.show_split_chooser_for_layout(host.ui.self,
                                                    KR_SPLIT_ROWS) == KR_OK);
  }
  MultiPageHost old(offsetof(KrShellUI, show_split_chooser_for_layout));
  KR_EXPECT(!KR_HAS_SLOT(&old.ui, KrShellUI, show_split_chooser_for_layout));
  KR_EXPECT(old.ui.show_split_chooser_for_layout == nullptr);
  KR_EXPECT(old.ui.layout_glance_card != nullptr);
}
KR_TEST(MultiPageAbiSnapLayoutsRoundTripThroughSizedRecords) {
  MultiPageHost host;
  for (int layout = KR_SPLIT_COLUMNS; layout <= KR_SPLIT_MAIN_BOTTOM;
       ++layout) {
    GroupPayload output;
    const auto preset = static_cast<KrSplitLayout>(layout);
    KrSplitCreateRequest request{
        sizeof(KrSplitCreateRequest), 7,  3,     output.members.data(), 3,
        sizeof(KrSplitMember),        11, preset};
    KR_ASSERT(host.ui.make_split(host.ui.self, &request, &output.group) ==
              KR_OK);
    KR_EXPECT(output.group.layout == preset);
    GeometryPayload geometry;
    KrSplitLayoutRequest bounds{sizeof(KrSplitLayoutRequest),
                                &output.group,
                                {0, 0, 1200, 800},
                                4,
                                false};
    KR_EXPECT(host.ui.layout_split(host.ui.self, &bounds, &geometry.geometry) ==
              KR_OK);
    KR_EXPECT(geometry.geometry.pane_count == 3);
    KR_EXPECT(geometry.geometry.divider_count == 2);
  }
}

KR_TEST(MultiPageAbiLegacyGlanceGeometryRemainsAvailable) {
  MultiPageHost host(offsetof(KrShellUI, layout_glance_card));
  KR_EXPECT(!KR_HAS_SLOT(&host.ui, KrShellUI, layout_glance_card));
  KR_EXPECT(host.ui.layout_glance_card == nullptr);
  KrGlanceGeometry output{.struct_size = sizeof(KrGlanceGeometry)};
  KR_EXPECT(host.ui.layout_glance(host.ui.self, {0, 0, 1000, 600}, &output) ==
            KR_OK);
  KR_EXPECT(output.frame.x == 24 && output.frame.y == 24);
  KR_EXPECT(output.toolbar.height == 36);
  KR_EXPECT(output.page.y == 60 && output.page.width == 952);
}

KR_TEST(MultiPageAbiDoesNotWritePastOlderModuleTableCapacity) {
  MultiPageHost host(offsetof(KrShellUI, multi_page_capabilities));
  KR_EXPECT(!KR_HAS_SLOT(&host.ui, KrShellUI, multi_page_capabilities));
  KR_EXPECT(host.ui.multi_page_capabilities == nullptr);
  KR_EXPECT(host.ui.reduce_glance == nullptr);
}

KR_TEST(MultiPageAbiRejectsMalformedInputAndOutputAtomically) {
  MultiPageHost host;
  GroupPayload input;
  GeometryPayload output;
  KrSplitLayoutRequest request{
      sizeof(KrSplitLayoutRequest), &input.group, {0, 0, 1000, 800}, 4, false};
  output.panes[0].tab = 99;
  input.members[3].tab = 11;
  KR_EXPECT(host.ui.layout_split(host.ui.self, &request, &output.geometry) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(output.panes[0].tab == 99);
  input.members[3].tab = 14;
  output.dividers[2].struct_size = offsetof(KrDividerBounds, horizontal);
  KR_EXPECT(host.ui.layout_split(host.ui.self, &request, &output.geometry) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(output.panes[0].tab == 99);
  output.dividers[2].struct_size = sizeof(KrDividerBounds);
  input.group.member_size = sizeof(KrSplitMember) - 1;
  KR_EXPECT(host.ui.layout_split(host.ui.self, &request, &output.geometry) ==
            KR_ERR_INVALID_ARGUMENT);
  input.group.member_size = sizeof(KrSplitMember);
  input.group.member_count = 5;
  KR_EXPECT(host.ui.layout_split(host.ui.self, &request, &output.geometry) ==
            KR_ERR_INVALID_ARGUMENT);
  input.group.member_count = 4;
  request.struct_size = offsetof(KrSplitLayoutRequest, right_to_left);
  KR_EXPECT(host.ui.layout_split(host.ui.self, &request, &output.geometry) ==
            KR_ERR_INVALID_ARGUMENT);
}

KR_TEST(MultiPageAbiSupportsExtendedStridesAndPreservesTrailingBytes) {
  MultiPageHost host;
  GroupPayload input;
  struct ExtendedMember {
    KrSplitMember base;
    std::uint64_t extra;
  };
  std::array<ExtendedMember, 4> members{};
  struct ExtendedPane {
    KrPaneBounds base;
    std::uint64_t extra;
  };
  std::array<ExtendedPane, 4> panes{};
  for (std::size_t i = 0; i < 4; ++i) {
    members[i] = {{sizeof(ExtendedMember), i + 11}, 321};
    panes[i] = {{sizeof(ExtendedPane), 99, {}}, 123};
  }
  input.group.members = &members[0].base;
  input.group.member_size = sizeof(ExtendedMember);
  GeometryPayload output;
  output.geometry.panes = &panes[0].base;
  output.geometry.pane_size = sizeof(ExtendedPane);
  const KrSplitLayoutRequest request{
      sizeof(KrSplitLayoutRequest), &input.group, {0, 0, 1000, 800}, 4, false};
  KR_EXPECT(host.ui.layout_split(host.ui.self, &request, &output.geometry) ==
            KR_OK);
  for (std::size_t i = 0; i < 4; ++i) {
    KR_EXPECT(panes[i].base.tab == i + 11);
    KR_EXPECT(panes[i].base.struct_size == sizeof(ExtendedPane));
    KR_EXPECT(panes[i].extra == 123);
    KR_EXPECT(members[i].extra == 321);
  }
}

KR_TEST(MultiPageAbiReducersRejectStaleStateAndSupportDissolution) {
  MultiPageHost host;
  GroupPayload input;
  GroupPayload output;
  KrSplitEdit edit{sizeof(KrSplitEdit), KR_SPLIT_MOVE, 0, 11, 3, 3,
                   KR_SPLIT_GRID,       0.5f};
  KR_EXPECT(host.ui.reduce_split(host.ui.self, &input.group, &edit,
                                 &output.group) == KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(output.group.revision == 1 && output.members[0].tab == 11);
  edit.revision = 1;
  KR_EXPECT(host.ui.reduce_split(host.ui.self, &input.group, &edit,
                                 &output.group) == KR_OK);
  KR_EXPECT(output.group.revision == 2 && output.members[3].tab == 11);
  edit.kind = KR_SPLIT_UNSPLIT;
  output.group.member_count = 4;
  KR_EXPECT(host.ui.reduce_split(host.ui.self, &input.group, &edit,
                                 &output.group) == KR_OK);
  KR_EXPECT(output.group.id == 0 && output.group.member_count == 0);
}

KR_TEST(MultiPageAbiGlanceValidatesPhaseAndLifetime) {
  MultiPageHost host;
  KrGlanceState state{.struct_size = sizeof(KrGlanceState)};
  KrGlanceState output{.struct_size = sizeof(KrGlanceState)};
  KrGlanceEdit edit{sizeof(KrGlanceEdit), KR_GLANCE_CREATE, 11, 12, 1, 0, 1};
  KR_EXPECT(host.ui.reduce_glance(host.ui.self, &state, &edit, &output) ==
            KR_OK);
  KR_EXPECT(output.phase == KR_GLANCE_OPEN && output.child == 12);
  state = output;
  edit.kind = KR_GLANCE_SPLIT;
  edit.parent_members = 4;
  KR_EXPECT(host.ui.reduce_glance(host.ui.self, &state, &edit, &output) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(output.phase == KR_GLANCE_OPEN);
  edit.kind = KR_GLANCE_REQUEST_CLOSE;
  edit.departure = 13;
  KR_EXPECT(host.ui.reduce_glance(host.ui.self, &state, &edit, &output) ==
            KR_OK);
  KR_EXPECT(output.phase == KR_GLANCE_CLOSING && output.departure == 13);
  state = output;
  edit.kind = KR_GLANCE_CANCEL_CLOSE;
  KR_EXPECT(host.ui.reduce_glance(host.ui.self, &state, &edit, &output) ==
            KR_OK);
  KR_EXPECT(output.phase == KR_GLANCE_OPEN && output.departure == 0);
  edit.generation = 0;
  KR_EXPECT(host.ui.reduce_glance(host.ui.self, &state, &edit, &output) ==
            KR_ERR_INVALID_ARGUMENT);
}
}  // namespace
}  // namespace kr
