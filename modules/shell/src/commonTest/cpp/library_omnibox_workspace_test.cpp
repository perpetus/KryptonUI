#include <limits>

#include "krypton/testing/test.hpp"
#include "krypton/ui/model/workspace_symbols.hpp"
#include "krypton/ui/state/store.hpp"

namespace kr {
KR_TEST(WorkspaceEmptyGlyphGetsAUsableDefault) {
  Store store;
  store.SetSpaces({Space{.id = SpaceId{1}, .name = "Default"}}, SpaceId{1});
  KR_EXPECT_EQ(store.spaces().front().glyph, kDefaultWorkspaceSymbol);
  KR_EXPECT(WorkspaceSymbolMatches(kWorkspaceSymbols.front(), "HOME"));
  KR_EXPECT(!WorkspaceSymbolMatches(kWorkspaceSymbols.front(), "rocket"));
}

KR_TEST(DownloadStartSignalIsNotReplayedByInventoryOrProgress) {
  LibraryState library;
  Download download{.id = DownloadId{1}, .flags = DownloadFlag::kProgressKnown};
  KR_ASSERT(library.UpsertDownload(download).has_value());
  KR_EXPECT_EQ(library.download_start_serial, 0u);
  download.flags.Set(DownloadFlag::kStarted, true);
  KR_ASSERT(library.UpsertDownload(download).has_value());
  KR_EXPECT_EQ(library.download_start_serial, 1u);
  download.progress = .4f;
  KR_ASSERT(library.UpsertDownload(download).has_value());
  KR_EXPECT_EQ(library.download_start_serial, 1u);
  download.flags.Set(DownloadFlag::kStarted, false);
  KR_ASSERT(library.UpsertDownload(download).has_value());
  KR_EXPECT_EQ(library.download_start_serial, 1u);
}

KR_TEST(OmniboxSelectionOverridesDefaultAcrossResultBatches) {
  Store store;
  store.OpenCommandBar("query", false);
  const auto request = store.SetCommandText("query");
  const Suggestion first{.is_default = true, .contents = "first"};
  Suggestion second{.contents = "second", .selected = true};
  store.SetSuggestions(request, {first, second}, 1);
  KR_EXPECT_EQ(store.ui().command_bar.selected, 1);
  second.selected = false;
  store.SetSuggestions(request, {first, second}, 2);
  KR_EXPECT_EQ(store.ui().command_bar.selected, 0);
  store.SetCommandSelection(std::numeric_limits<std::size_t>::max());
  KR_EXPECT_EQ(store.ui().command_bar.selected, -1);
}
}  // namespace kr
