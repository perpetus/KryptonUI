#include "krypton/ui/model/tab_drop.hpp"

#include <array>
#include <optional>
#include <vector>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {

std::array<Tab, 4> Tabs() {
  return {Tab{.id = TabId{1}}, Tab{.id = TabId{2}}, Tab{.id = TabId{3}},
          Tab{.id = TabId{4}}};
}

KR_TEST(TabDropIndexCountsOnlyTheOtherTabs) {
  const auto tabs = Tabs();
  // 1 dropped before 4 lands after 2 and 3.
  KR_EXPECT(TabDropIndex(tabs, TabId{1}, TabId{3}, TabId{4}) ==
            std::optional<std::size_t>(2));
  // 4 dropped before 2 lands after 1.
  KR_EXPECT(TabDropIndex(tabs, TabId{4}, TabId{1}, TabId{2}) ==
            std::optional<std::size_t>(1));
  KR_EXPECT(TabDropIndex(tabs, TabId{3}, {}, TabId{1}) ==
            std::optional<std::size_t>(0));
}

KR_TEST(TabDropIndexFallsBackToThePreviousTab) {
  const auto tabs = Tabs();
  KR_EXPECT(TabDropIndex(tabs, TabId{1}, TabId{4}, {}) ==
            std::optional<std::size_t>(3));
  KR_EXPECT(TabDropIndex(tabs, TabId{1}, TabId{2}, TabId{99}) ==
            std::optional<std::size_t>(1));
  KR_EXPECT(!TabDropIndex(tabs, TabId{1}, {}, {}));
  KR_EXPECT(!TabDropIndex(tabs, TabId{1}, TabId{98}, TabId{99}));
}

struct DropHost {
  KrShellUI ui{};
  struct Move {
    KrTabId tab;
    KrSpaceId space;
    std::size_t index;
  };
  std::vector<Move> moves;
  std::uint32_t flag_calls = 0, mask = 0, values = 0;
  bool publish_on_pin = false;

  [[nodiscard]] KrResult Create() {
    headless::Reset();
    KrBrowser table{};
    table.struct_size = sizeof(table);
    table.self = this;
    table.move_tab = [](void* self, KrTabId tab, KrSpaceId space,
                        std::size_t index) {
      static_cast<DropHost*>(self)->moves.push_back({tab, space, index});
    };
    table.set_tab_flags = [](void* self, KrTabId id, std::uint32_t mask,
                             std::uint32_t values) {
      auto& host = *static_cast<DropHost*>(self);
      ++host.flag_calls;
      host.mask = mask;
      host.values = values;
      if (host.publish_on_pin) {
        const KrTab tab{.struct_size = sizeof(KrTab),
                        .id = id,
                        .space = 77,
                        .flags = values};
        host.ui.tab_updated(host.ui.self, &tab);
      }
    };
    const KrCreateParams params{sizeof(KrCreateParams), KR_UI_ABI_VERSION,
                                nullptr, &table};
    ui.struct_size = sizeof(ui);
    return kr_ui_create(&params, &ui);
  }
  void Insert(KrTabId id, std::uint32_t flags = 0) {
    const KrTab tab{.struct_size = sizeof(KrTab), .id = id, .flags = flags};
    ui.tab_inserted(ui.self, static_cast<std::size_t>(id - 1), &tab);
  }
  ~DropHost() {
    if (ui.self)
      ui.destroy(ui.self);
    headless::Reset();
  }
};

KR_TEST(TabDropAsksTheHostToMoveWithoutMovingTheStore) {
  DropHost host;
  KR_ASSERT(host.Create() == KR_OK);
  for (KrTabId id = 1; id <= 3; ++id)
    host.Insert(id);
  headless::RunPendingTasks();
  headless::Delegate()->OnTabDropped(TabId{1}, TabId{3}, {}, false);
  KR_ASSERT(host.moves.size() == 1u);
  KR_EXPECT_EQ(host.moves[0].tab, 1u);
  KR_EXPECT_EQ(host.moves[0].index, 2u);
  KR_EXPECT_EQ(host.flag_calls, 0u);
}

KR_TEST(TabDropIntoThePinnedSectionPinsTheTab) {
  DropHost host;
  KR_ASSERT(host.Create() == KR_OK);
  host.Insert(1, KR_TAB_PINNED);
  host.Insert(2);
  host.Insert(3);
  headless::RunPendingTasks();
  headless::Delegate()->OnTabDropped(TabId{3}, TabId{1}, TabId{2}, true);
  KR_EXPECT_EQ(host.flag_calls, 1u);
  KR_EXPECT_EQ(host.mask, std::uint32_t{KR_TAB_PINNED});
  KR_EXPECT_EQ(host.values, std::uint32_t{KR_TAB_PINNED});
  KR_ASSERT(host.moves.size() == 1u);
  KR_EXPECT_EQ(host.moves[0].index, 1u);
}

KR_TEST(TabDropThatChangesNothingRestoresTheList) {
  DropHost host;
  KR_ASSERT(host.Create() == KR_OK);
  host.Insert(1);
  host.Insert(2);
  headless::RunPendingTasks();
  const auto renders = headless::RenderCount();
  headless::Delegate()->OnTabDropped(TabId{1}, {}, TabId{2}, false);
  headless::Delegate()->OnTabDropped(TabId{99}, {}, TabId{2}, false);
  KR_EXPECT(host.moves.empty());
  KR_EXPECT_EQ(host.flag_calls, 0u);
  headless::RunPendingTasks();
  KR_EXPECT(headless::RenderCount() > renders);
  KR_EXPECT(headless::LastRender().changed.Has(Dirty::kTabs));
}

KR_TEST(TabDropIgnoredByTheHostRestoresTheList) {
  DropHost host;
  KR_ASSERT(host.Create() == KR_OK);
  host.Insert(1);
  host.Insert(2);
  headless::RunPendingTasks();
  const auto renders = headless::RenderCount();
  headless::Delegate()->OnTabDropped(TabId{1}, TabId{2}, {}, false);
  KR_ASSERT(host.moves.size() == 1u);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::RenderCount(), renders + 1);
  KR_EXPECT(headless::LastRender().changed.Has(Dirty::kTabs));
}

KR_TEST(TabDropCopiesTheSpaceBeforeCallingTheHost) {
  DropHost host;
  KR_ASSERT(host.Create() == KR_OK);
  host.Insert(1);
  host.Insert(2);
  host.publish_on_pin = true;
  headless::RunPendingTasks();
  headless::Delegate()->OnTabDropped(TabId{2}, {}, TabId{1}, true);
  KR_ASSERT(host.moves.size() == 1u);
  KR_EXPECT_EQ(host.moves[0].space, 0u);
  KR_EXPECT_EQ(host.moves[0].index, 0u);
}

}  // namespace
}  // namespace kr
