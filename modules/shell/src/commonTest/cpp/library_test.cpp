#include "krypton/ui/presentation/library.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {
KrStr Text(std::string_view text) {
  return {text.data(), text.size()};
}

struct Host {
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};
  struct Call {
    std::uint64_t id;
    std::uint32_t command;
  };
  std::vector<Call> downloads, media, library;
  std::vector<double> seeks;
  std::vector<std::pair<std::uint32_t, bool>> visibility;

  explicit Host(std::size_t size = sizeof(KrBrowser)) {
    headless::Reset();
    KrBrowser browser{.struct_size = size, .self = this};
    browser.download_command = [](void* self, KrDownloadId id,
                                  std::uint32_t command) {
      static_cast<Host*>(self)->downloads.push_back({id, command});
    };
    browser.media_command = [](void* self, KrMediaId id,
                               std::uint32_t command) {
      static_cast<Host*>(self)->media.push_back({id, command});
    };
    browser.media_seek = [](void* self, KrMediaId, double seconds) {
      static_cast<Host*>(self)->seeks.push_back(seconds);
    };
    browser.library_visibility_changed = [](void* self, std::uint32_t section,
                                            bool visible) {
      static_cast<Host*>(self)->visibility.emplace_back(section, visible);
    };
    browser.library_command = [](void* self, std::uint32_t, KrLibraryItemId id,
                                 std::uint32_t command) {
      static_cast<Host*>(self)->library.push_back({id, command});
    };
    KrCreateParams params{sizeof(KrCreateParams), KR_UI_ABI_VERSION, nullptr,
                          &browser};
    KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
  }
  ~Host() { ui.destroy(ui.self); }

  KrResult Download(std::uint64_t id,
                    std::string_view name,
                    std::uint32_t state = KR_DOWNLOAD_COMPLETE,
                    std::uint32_t flags = KR_DOWNLOAD_FILE_EXISTS,
                    float progress = 1) {
    const KrDownload item{
        sizeof(KrDownload), id,         state,          flags,
        progress,           Text(name), Text("1.2 MB"), KrImageId{0}};
    return ui.upsert_download(ui.self, &item);
  }
  KrResult Media(std::uint64_t id,
                 KrTabId tab,
                 std::uint32_t flags,
                 std::string_view title = "Song") {
    const KrMediaSession item{
        sizeof(KrMediaSession), id, tab, flags, Text(title),
        Text("Artist"),         0,  30,  180,   1};
    return ui.upsert_media_session(ui.self, &item);
  }
  KrResult Item(std::uint64_t id,
                std::uint32_t section,
                std::string_view title,
                std::string_view group = {},
                KrLibraryItemId before = 0) {
    const KrLibraryItem item{
        sizeof(KrLibraryItem), id,          section,     0,
        Text(title),           Text("sub"), Text(group), 0};
    return ui.upsert_library_item(ui.self, &item, before);
  }
  KrResult Section(std::uint32_t section, std::uint32_t flags) {
    const KrLibrarySectionState state{sizeof(KrLibrarySectionState), section,
                                      flags};
    return ui.set_library_section(ui.self, &state);
  }
  void Tab(KrTabId id, std::size_t index) {
    const KrTab tab{sizeof(KrTab), id, 1, 0, Text("Tab"), Text("https://a"), 0};
    ui.tab_inserted(ui.self, index, &tab);
  }
};

const headless::RenderSnapshot::LibrarySnapshot& Library() {
  headless::RunPendingTasks();
  return headless::LastRender().library;
}
}  // namespace

KR_TEST(DownloadStackShowsTheNewestFourOldestFirst) {
  Host host;
  for (std::uint64_t id = 1; id <= 5; ++id)
    KR_ASSERT(host.Download(id, std::format("file{}.zip", id)) == KR_OK);
  const auto& library = Library();
  KR_ASSERT(library.stack.size() == 4);
  KR_EXPECT_EQ(library.stack.front(), "file2.zip");
  KR_EXPECT_EQ(library.stack.back(), "file5.zip");
  KR_EXPECT(!library.downloading);
  KR_ASSERT(host.ui.remove_download(host.ui.self, 5) == KR_OK);
  KR_EXPECT(host.ui.remove_download(host.ui.self, 5) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT_EQ(Library().stack.back(), "file4.zip");
}

KR_TEST(PendingDownloadsBadgeTheLibraryAndOfferOpenWhenDone) {
  Host host;
  KR_ASSERT(host.Download(1, "done.pdf") == KR_OK);
  KR_ASSERT(
      host.Download(2, "movie.mkv", KR_DOWNLOAD_IN_PROGRESS,
                    KR_DOWNLOAD_PROGRESS_KNOWN | KR_DOWNLOAD_OPEN_WHEN_DONE,
                    0.4f) == KR_OK);
  const auto& library = Library();
  KR_EXPECT(library.downloading);
  KR_EXPECT_EQ(library.stack_status.back(), "Opens when finished");
  KR_EXPECT_EQ(library.stack_status.front(), "1.2 MB");
  KR_EXPECT(host.Download(3, "bad", 9) == KR_ERR_INVALID_ARGUMENT);
  headless::Delegate()->OnDownloadCommand(DownloadId{2},
                                          DownloadCommand::kCancel);
  headless::Delegate()->OnDownloadCommand(DownloadId{99},
                                          DownloadCommand::kOpen);
  KR_ASSERT(host.downloads.size() == 1);
  KR_EXPECT_EQ(host.downloads[0].id, 2u);
  KR_EXPECT_EQ(host.downloads[0].command,
               static_cast<std::uint32_t>(KR_DOWNLOAD_CANCEL));
}

KR_TEST(MediaCardsRevealAfterPlaybackLastsAndSkipTheActiveTab) {
  Host host;
  host.Tab(10, 0);
  host.Tab(11, 1);
  host.ui.set_active_tab(host.ui.self, 10);
  KR_ASSERT(host.Media(1, 11, KR_MEDIA_PLAYING | KR_MEDIA_CAN_SEEK) == KR_OK);
  KR_EXPECT(Library().media.empty());
  headless::AdvanceTime(std::chrono::milliseconds(1000));
  KR_ASSERT(Library().media.size() == 1);
  KR_EXPECT_EQ(Library().media[0], "Song");
  KR_EXPECT(Library().media_cards[0].position_visible);
  host.ui.set_active_tab(host.ui.self, 11);
  KR_EXPECT(Library().media.empty());
  host.ui.set_active_tab(host.ui.self, 10);
  KR_EXPECT_EQ(Library().media.size(), 1u);

  // A ping that stops before the delay never shows a card.
  KR_ASSERT(host.Media(2, 11, KR_MEDIA_PLAYING, "Ping") == KR_OK);
  KR_ASSERT(host.Media(2, 11, 0, "Ping") == KR_OK);
  headless::AdvanceTime(std::chrono::milliseconds(1000));
  KR_EXPECT_EQ(Library().media.size(), 1u);

  // Sharing shows at once and in front, even for the active tab.
  KR_ASSERT(host.Media(3, 10, KR_MEDIA_SHARING | KR_MEDIA_CAN_TOGGLE_CAMERA,
                       "Call") == KR_OK);
  KR_ASSERT(Library().media.size() == 2);
  KR_EXPECT_EQ(Library().media[0], "Call");
  KR_EXPECT(!Library().media_cards[0].position_visible);
  // Capture-only cards show device state without offering a dead toggle.
  KR_EXPECT(Library().media_cards[0].can_toggle_camera);
  KR_EXPECT(!Library().media_cards[0].can_toggle_microphone);
}

KR_TEST(MediaCommandsAndSeeksReachTheHost) {
  Host host;
  KR_ASSERT(host.Media(1, 11,
                       KR_MEDIA_PLAYING | KR_MEDIA_CAN_SEEK |
                           KR_MEDIA_CAN_PLAY_PAUSE) == KR_OK);
  KR_ASSERT(host.Media(2, 12, KR_MEDIA_PLAYING) == KR_OK);
  headless::Delegate()->OnMediaCommand(MediaId{1}, MediaCommand::kPlayPause);
  headless::Delegate()->OnMediaCommand(MediaId{9}, MediaCommand::kNext);
  headless::Delegate()->OnMediaSeek(MediaId{1}, 400);
  headless::Delegate()->OnMediaSeek(MediaId{1},
                                    std::numeric_limits<double>::quiet_NaN());
  headless::Delegate()->OnMediaSeek(MediaId{1},
                                    std::numeric_limits<double>::infinity());
  headless::Delegate()->OnMediaSeek(MediaId{2}, 20);
  KR_ASSERT(host.media.size() == 1);
  KR_EXPECT_EQ(host.media[0].command,
               static_cast<std::uint32_t>(KR_MEDIA_PLAY_PAUSE));
  KR_ASSERT(host.seeks.size() == 1);
  KR_EXPECT_EQ(host.seeks[0], 180.0);
  KR_ASSERT(host.ui.remove_media_session(host.ui.self, 1) == KR_OK);
  KR_EXPECT(host.ui.remove_media_session(host.ui.self, 1) ==
            KR_ERR_INVALID_ARGUMENT);
}

KR_TEST(MediaRevealIgnoresCallbacksForRemovedSessions) {
  Host host;
  KR_ASSERT(host.Media(1, 11, KR_MEDIA_PLAYING, "Old session") == KR_OK);
  headless::AdvanceTime(std::chrono::milliseconds(500));
  KR_ASSERT(host.ui.remove_media_session(host.ui.self, 1) == KR_OK);
  KR_ASSERT(host.Media(1, 11, KR_MEDIA_PLAYING, "New session") == KR_OK);
  headless::AdvanceTime(std::chrono::milliseconds(500));
  KR_EXPECT(Library().media.empty());
  headless::AdvanceTime(std::chrono::milliseconds(500));
  KR_ASSERT(Library().media.size() == 1);
  KR_EXPECT_EQ(Library().media.front(), "New session");
}

KR_TEST(MediaRevealRestartsAfterAnInterruptedShortSound) {
  Host host;
  KR_ASSERT(host.Media(1, 11, KR_MEDIA_PLAYING) == KR_OK);
  headless::AdvanceTime(std::chrono::milliseconds(500));
  KR_ASSERT(host.Media(1, 11, 0) == KR_OK);
  headless::AdvanceTime(std::chrono::milliseconds(100));
  KR_ASSERT(host.Media(1, 11, KR_MEDIA_PLAYING) == KR_OK);
  headless::AdvanceTime(std::chrono::milliseconds(400));
  KR_EXPECT(Library().media.empty());
  headless::AdvanceTime(std::chrono::milliseconds(600));
  KR_EXPECT_EQ(Library().media.size(), 1u);
}

KR_TEST(SharingKeepsItsCardVisibleAfterAPendingPlaybackReveal) {
  Host host;
  KR_ASSERT(host.Media(1, 11, KR_MEDIA_PLAYING) == KR_OK);
  headless::AdvanceTime(std::chrono::milliseconds(500));
  KR_ASSERT(host.Media(1, 11, KR_MEDIA_SHARING) == KR_OK);
  KR_EXPECT_EQ(Library().media.size(), 1u);
  headless::AdvanceTime(std::chrono::milliseconds(500));
  KR_EXPECT_EQ(Library().media.size(), 1u);
}

KR_TEST(MediaCommandsRespectCurrentHostCapabilities) {
  Host host;
  KR_ASSERT(host.Media(1, 11, KR_MEDIA_SHARING) == KR_OK);
  const auto command = [](MediaCommand value) {
    headless::Delegate()->OnMediaCommand(MediaId{1}, value);
  };
  for (const auto value :
       {MediaCommand::kPlayPause, MediaCommand::kPrevious, MediaCommand::kNext,
        MediaCommand::kTogglePictureInPicture, MediaCommand::kToggleMicrophone,
        MediaCommand::kToggleCamera})
    command(value);
  KR_EXPECT(host.media.empty());

  KR_ASSERT(host.Media(1, 11,
                       KR_MEDIA_SHARING | KR_MEDIA_CAN_TOGGLE_MICROPHONE |
                           KR_MEDIA_CAN_TOGGLE_CAMERA) == KR_OK);
  command(MediaCommand::kToggleMicrophone);
  command(MediaCommand::kToggleCamera);
  KR_ASSERT(host.media.size() == 2);
  KR_EXPECT_EQ(host.media[0].command,
               static_cast<std::uint32_t>(KR_MEDIA_TOGGLE_MICROPHONE));
  KR_EXPECT_EQ(host.media[1].command,
               static_cast<std::uint32_t>(KR_MEDIA_TOGGLE_CAMERA));
  KR_ASSERT(host.Media(1, 11, KR_MEDIA_SHARING) == KR_OK);
  command(MediaCommand::kToggleMicrophone);
  command(MediaCommand::kToggleCamera);
  KR_EXPECT_EQ(host.media.size(), 2u);
}

KR_TEST(LibraryMediaAsksToOptInThenShowsHostItems) {
  Host host;
  KR_ASSERT(host.ui.show_library(host.ui.self, KR_LIBRARY_MEDIA) == KR_OK);
  auto library = Library();
  KR_EXPECT(library.open);
  KR_EXPECT(library.section == LibrarySection::kMedia);
  KR_EXPECT(library.media_opt_in);
  KR_EXPECT_EQ(library.sections.size(), 5u);
  KR_ASSERT(host.visibility.size() == 1);
  KR_EXPECT_EQ(host.visibility[0].first,
               static_cast<std::uint32_t>(KR_LIBRARY_MEDIA));
  KR_EXPECT(host.visibility[0].second);

  headless::Delegate()->OnLibraryCommand(LibrarySection::kMedia, {},
                                         LibraryCommand::kEnable);
  KR_ASSERT(host.library.size() == 1);
  KR_EXPECT_EQ(host.library[0].command,
               static_cast<std::uint32_t>(KR_LIBRARY_ENABLE));
  KR_ASSERT(host.Section(KR_LIBRARY_MEDIA, KR_LIBRARY_SECTION_ENABLED |
                                               KR_LIBRARY_SECTION_LOADING) ==
            KR_OK);
  library = Library();
  KR_EXPECT(!library.media_opt_in);
  KR_EXPECT(library.empty == UiText::kLibraryMediaLoading);
  KR_ASSERT(host.Section(KR_LIBRARY_MEDIA, KR_LIBRARY_SECTION_ENABLED) ==
            KR_OK);
  KR_ASSERT(host.Item(1, KR_LIBRARY_MEDIA, "shot.png") == KR_OK);
  KR_ASSERT(host.Item(2, KR_LIBRARY_MEDIA, "clip.mp4", {}, 1) == KR_OK);
  library = Library();
  KR_ASSERT(library.tiles.size() == 2);
  KR_EXPECT_EQ(library.tiles[0].title, "clip.mp4");
  KR_EXPECT(library.empty == UiText::kCount);
  headless::Delegate()->OnLibraryCommand(
      LibrarySection::kMedia, LibraryItemId{2}, LibraryCommand::kOpen);
  KR_EXPECT_EQ(host.library.back().id, 2u);
}

KR_TEST(LibraryHistoryGroupsRowsAndReportsVisibility) {
  Host host;
  KR_ASSERT(host.ui.show_library(host.ui.self, KR_LIBRARY_DOWNLOADS) == KR_OK);
  KR_EXPECT(Library().empty == UiText::kLibraryDownloadsEmpty);
  headless::Delegate()->OnLibrarySectionSelected(LibrarySection::kHistory);
  KR_ASSERT(host.Item(1, KR_LIBRARY_HISTORY, "One", "Today") == KR_OK);
  KR_ASSERT(host.Item(2, KR_LIBRARY_HISTORY, "Two", "Today") == KR_OK);
  KR_ASSERT(host.Item(3, KR_LIBRARY_HISTORY, "Three", "Yesterday") == KR_OK);
  KR_EXPECT(host.Item(3, KR_LIBRARY_BOOSTS, "Moved") ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(host.Item(4, KR_LIBRARY_SPACES, "Space") ==
            KR_ERR_INVALID_ARGUMENT);
  const auto& rows = Library().rows;
  KR_ASSERT(rows.size() == 5);
  KR_EXPECT(rows[0].kind == LibraryRowKind::kHeading);
  KR_EXPECT_EQ(rows[0].title, "Today");
  KR_EXPECT_EQ(rows[2].title, "Two");
  KR_EXPECT_EQ(rows[3].title, "Yesterday");
  KR_ASSERT(host.visibility.size() == 3);
  KR_EXPECT(!host.visibility[1].second);
  KR_EXPECT_EQ(host.visibility[2].first,
               static_cast<std::uint32_t>(KR_LIBRARY_HISTORY));

  // A new tab, e.g. one opened from history, closes the library.
  host.Tab(20, 0);
  KR_EXPECT(!Library().open);
  KR_ASSERT(host.visibility.size() == 4);
  KR_EXPECT(!host.visibility[3].second);
}

KR_TEST(LibraryHiddenSectionsLeaveTheRail) {
  Host host;
  KR_ASSERT(host.Section(KR_LIBRARY_SPACES, KR_LIBRARY_SECTION_HIDDEN) ==
            KR_OK);
  KR_EXPECT(host.ui.show_library(host.ui.self, KR_LIBRARY_SPACES) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(host.ui.show_library(host.ui.self, 7) == KR_ERR_INVALID_ARGUMENT);
  headless::Delegate()->OnLibraryToggled();
  const auto& library = Library();
  KR_EXPECT(library.open);
  KR_EXPECT(library.section == LibrarySection::kHistory);
  KR_EXPECT_EQ(library.sections.size(), 4u);
  headless::Delegate()->OnLibrarySectionSelected(
      static_cast<LibrarySection>(99));
  KR_EXPECT(Library().section == LibrarySection::kHistory);
  headless::Delegate()->OnLibraryToggled();
  KR_EXPECT(!Library().open);
}

KR_TEST(LibraryRejectsIncompleteRecords) {
  Host host;
  KrDownload download{
      offsetof(KrDownload, icon), 1, 0, 0, 0, Text("a"), Text("b"), 0};
  KR_EXPECT(host.ui.upsert_download(host.ui.self, &download) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(host.ui.upsert_download(host.ui.self, nullptr) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(host.Media(0, 1, 0) == KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(host.Section(9, 0) == KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(host.Item(1, KR_LIBRARY_HISTORY, "x", {}, 42) ==
            KR_ERR_INVALID_ARGUMENT);
}

KR_TEST(LibraryAccentClampsOklchLightness) {
  const auto light = AccentButtonColor(Color{0xFFFFFFFFu}, false);
  KR_EXPECT(light.r() >= 127 && light.r() <= 129);
  KR_EXPECT_EQ(light.r(), light.g());
  KR_EXPECT_EQ(light.g(), light.b());
  const auto dark = AccentButtonColor(Color{0xFF000000u}, true);
  KR_EXPECT(dark.r() > 100 && dark.r() < light.r());
  const auto mid = AccentButtonColor(Color{0xFF7E6A86u}, false);
  KR_EXPECT(mid.r() > 100 && mid.b() > mid.g());
}

KR_TEST(MediaTimesFollowZenFormatting) {
  KR_EXPECT_EQ(FormatMediaTime(0), "0:00");
  KR_EXPECT_EQ(FormatMediaTime(59.2), "1:00");
  KR_EXPECT_EQ(FormatMediaTime(65), "1:05");
  KR_EXPECT_EQ(FormatMediaTime(3661), "1:01:01");
}
}  // namespace kr
