#include "krypton/testing/test.hpp"
#include "krypton/ui/presentation/presenter.hpp"
#include "krypton/ui/shell/shell.hpp"
#include "krypton_ui/abi.h"

namespace kr {
KR_TEST(FoldersRejectCyclesMissingParentsCrossWorkspaceAndExcessDepth) {
  Store store;
  KR_ASSERT(store.UpsertFolder({FolderId{1}, SpaceId{1}, {}, "Root"}));
  KR_EXPECT(
      !store.UpsertFolder({FolderId{2}, SpaceId{1}, FolderId{9}, "Missing"}));
  KR_EXPECT(!store.UpsertFolder(
      {FolderId{2}, SpaceId{2}, FolderId{1}, "Wrong space"}));
  for (std::uint64_t id = 2; id <= kMaxFolderDepth; ++id)
    KR_ASSERT(store.UpsertFolder(
        {FolderId{id}, SpaceId{1}, FolderId{id - 1}, "Nested"}));
  KR_EXPECT(
      !store.UpsertFolder({FolderId{6}, SpaceId{1}, FolderId{5}, "Too deep"}));
  KR_EXPECT(
      !store.UpsertFolder({FolderId{1}, SpaceId{1}, FolderId{3}, "Cycle"}));
  KR_EXPECT(!store.FindFolder(FolderId{1})->parent);
}
KR_TEST(CollapsedFoldersKeepTheActiveBranchVisibleAndPreserveNestedCollapse) {
  Store store;
  store.SetSpaces({{.id = SpaceId{1}, .name = "Work"}}, SpaceId{1});
  KR_ASSERT(
      store.UpsertFolder({FolderId{1}, SpaceId{1}, {}, "Root", {}, 0, true}));
  KR_ASSERT(store.UpsertFolder(
      {FolderId{2}, SpaceId{1}, FolderId{1}, "Child", {}, 0, true}));
  KR_ASSERT(store.InsertTab(0, {.id = TabId{10},
                                .space = SpaceId{1},
                                .flags = TabFlag::kPinned,
                                .title = "Active",
                                .folder = FolderId{2}}));
  KR_ASSERT(store.InsertTab(1, {.id = TabId{11},
                                .space = SpaceId{1},
                                .flags = TabFlag::kPinned,
                                .title = "Hidden",
                                .folder = FolderId{2}}));
  KR_ASSERT(store.SetActiveTab(TabId{10}));
  Presenter presenter;
  const auto& vm = presenter.Present(store, {}, Dirty::kAll);
  KR_EXPECT(vm.pinned.empty());
  KR_EXPECT_EQ(vm.folder_rows.size(), 3u);
  KR_EXPECT_EQ(vm.folder_rows[0].folder, FolderId{1});
  KR_EXPECT_EQ(vm.folder_rows[1].folder, FolderId{2});
  KR_EXPECT(vm.folder_rows[0].contains_active);
  KR_EXPECT(vm.folder_rows[1].contains_active);
  KR_EXPECT_EQ(vm.folder_rows[2].tab, TabId{10});
  KR_EXPECT_EQ(vm.folder_rows[2].depth, 2u);
  KR_ASSERT(store.SetActiveTab({}));
  const auto& collapsed = presenter.Present(store, {}, Dirty::kActiveTab);
  KR_EXPECT(collapsed.changed.Has(Dirty::kTabs));
  KR_EXPECT_EQ(collapsed.folder_rows.size(), 1u);
  KR_EXPECT(!collapsed.folder_rows[0].contains_active);
  KR_EXPECT(store.FindFolder(FolderId{2})->collapsed);
  KR_ASSERT(store.RemoveFolder(FolderId{1}));
  const auto& unpacked = presenter.Present(store, {}, Dirty::kTabs);
  KR_EXPECT(unpacked.folders.empty());
  KR_EXPECT_EQ(unpacked.pinned.size(), 2u);
  KR_EXPECT(!store.FindTab(TabId{10})->folder);
}
KR_TEST(ExpandedFoldersDoNotReconcileRowsOnAnOrdinarySelectionChange) {
  Store store;
  store.SetSpaces({{.id = SpaceId{1}, .name = "Work"}}, SpaceId{1});
  KR_ASSERT(store.UpsertFolder({FolderId{1}, SpaceId{1}, {}, "Root"}));
  for (std::uint64_t id = 1; id <= 2; ++id)
    KR_ASSERT(store.InsertTab(store.tabs().size(), {.id = TabId{id},
                                                    .space = SpaceId{1},
                                                    .flags = TabFlag::kPinned,
                                                    .title = "Tab",
                                                    .folder = FolderId{1}}));
  KR_ASSERT(store.SetActiveTab(TabId{1}));
  Presenter presenter;
  (void)presenter.Present(store, {}, Dirty::kAll);
  KR_ASSERT(store.SetActiveTab(TabId{2}));
  const auto& vm = presenter.Present(store, {}, Dirty::kActiveTab);
  KR_EXPECT(!vm.changed.Has(Dirty::kTabs));
  KR_EXPECT(vm.folder_tabs[1].active);
}
KR_TEST(FolderIconActiveBranchChangesWithoutRebuildingExpandedRows) {
  Store store;
  store.SetSpaces({{.id = SpaceId{1}, .name = "Work"}}, SpaceId{1});
  KR_ASSERT(store.UpsertFolder({FolderId{1}, SpaceId{1}, {}, "Folder"}));
  KR_ASSERT(store.InsertTab(0, {.id = TabId{1},
                                .space = SpaceId{1},
                                .flags = TabFlag::kPinned,
                                .folder = FolderId{1}}));
  KR_ASSERT(store.InsertTab(1, {.id = TabId{2}, .space = SpaceId{1}}));
  KR_ASSERT(store.SetActiveTab(TabId{1}));
  Presenter presenter;
  KR_EXPECT(
      presenter.Present(store, {}, Dirty::kAll).folder_rows[0].contains_active);
  KR_ASSERT(store.SetActiveTab(TabId{2}));
  const auto& vm = presenter.Present(store, {}, Dirty::kActiveTab);
  KR_EXPECT(!vm.folder_rows[0].contains_active);
  KR_EXPECT(!vm.changed.Has(Dirty::kTabs));
}
KR_TEST(FolderAbiRejectsTruncationAndBorrowedStringsAreCopied) {
  KrBrowser browser{};
  browser.struct_size = sizeof(browser);
  KrShellUI ui{};
  ui.struct_size = sizeof(ui);
  const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                              &browser};
  KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
  KrFolder value{};
  value.struct_size = offsetof(KrFolder, collapsed);
  KR_EXPECT_EQ(ui.upsert_folder(ui.self, &value), KR_ERR_INVALID_ARGUMENT);
  std::string name = "Persistent folder";
  value = {sizeof(value), 1, 1, 0, {name.data(), name.size()}, {}, 0, false};
  KR_EXPECT_EQ(ui.upsert_folder(ui.self, &value), KR_OK);
  name.assign("Changed backing string");
  KR_EXPECT_EQ(
      static_cast<Shell*>(ui.self)->store().FindFolder(FolderId{1})->name,
      std::string("Persistent folder"));
  KrTab legacy{.struct_size = offsetof(KrTab, folder),
               .id = 10,
               .space = 1,
               .folder = 1};
  ui.tab_inserted(ui.self, 0, &legacy);
  KR_EXPECT(!static_cast<Shell*>(ui.self)->store().FindTab(TabId{10})->folder);
  KR_EXPECT_EQ(ui.remove_folder(ui.self, 1), KR_OK);
  KR_EXPECT_EQ(ui.remove_folder(ui.self, 1), KR_ERR_INVALID_ARGUMENT);
  ui.destroy(ui.self);
}
}  // namespace kr
