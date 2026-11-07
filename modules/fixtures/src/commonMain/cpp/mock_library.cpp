// Sample downloads, a sidebar media session and library sections for the
// preview. Names, sizes and pictures are fixture data, not real files.
#include "mock_library.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

#include "krypton/fixtures/mock_browser.hpp"

namespace kr::fixtures {
namespace {
constexpr KrImageId kLibraryImageBase = KrImageId{1} << 61;
constexpr std::int32_t kCommandBase = 0x4C00;
enum LibraryMenuCommand : std::int32_t {
  kShowInFolder = kCommandBase,
  kCancelDownload,
  kDeleteFile,
  kRemoveDownload,
  kReopenPage,
  kForgetPage,
  kOpenMedia,
};

struct Track {
  std::string_view title, artist;
  double duration;
};
constexpr std::array<Track, 3> kTracks{{
    {"Midnight City Lights", "Krypton Radio", 213},
    {"Slow Morning", "Krypton Radio", 187},
    {"Orbit", "Krypton Radio", 245},
}};

struct MediaFile {
  std::string_view name;
  int width, height;
  std::uint32_t from, to;
  bool video;
};
constexpr std::array<MediaFile, 9> kMediaFiles{{
    {"Screenshot 2026-10-24.png", 160, 100, 0xFF2B3A67, 0xFF8EA6FF, false},
    {"aurora.jpg", 150, 100, 0xFF0B3D3A, 0xFF5EE3B5, false},
    {"holiday.mp4", 160, 90, 0xFF5B2A86, 0xFFF29E4C, true},
    {"diagram.png", 120, 120, 0xFFE9EEF5, 0xFF9AA8BD, false},
    {"portrait.jpg", 100, 140, 0xFF6B3E26, 0xFFE0B48A, false},
    {"recording.mov", 160, 100, 0xFF1F2933, 0xFF52606D, true},
    {"sunset.png", 150, 100, 0xFFB23A48, 0xFFF7B267, false},
    {"mockup.png", 140, 110, 0xFFF4F4F6, 0xFF7C5CFF, false},
    {"snow.jpg", 150, 100, 0xFFDDE7F2, 0xFF7A99B8, false},
}};

KrStr View(std::string_view text) {
  return {text.data(), text.size()};
}

// A diagonal two-color picture, premultiplied BGRA.
std::vector<std::uint8_t> Picture(int width,
                                  int height,
                                  std::uint32_t from,
                                  std::uint32_t to) {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height *
                                   4);
  const auto channel = [](std::uint32_t color, int shift) {
    return static_cast<float>((color >> shift) & 0xFF);
  };
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const float t =
          (static_cast<float>(x) / width + static_cast<float>(y) / height) / 2;
      auto* pixel = &pixels[(static_cast<std::size_t>(y) * width + x) * 4];
      for (int c = 0; c < 3; ++c) {
        const int shift = c * 8;  // B, G, R
        pixel[c] = static_cast<std::uint8_t>(
            std::lround(channel(from, shift) +
                        (channel(to, shift) - channel(from, shift)) * t));
      }
      pixel[3] = 0xFF;
    }
  return pixels;
}

void Deliver(const KrShellUI& ui,
             std::uint32_t request,
             const std::vector<KrMenuItem>& items) {
  const KrMenu menu{sizeof(KrMenu), items.data(), items.size(),
                    sizeof(KrMenuItem)};
  if (KR_HAS_SLOT(&ui, KrShellUI, show_menu_with_glyphs) &&
      ui.show_menu_with_glyphs)
    ui.show_menu_with_glyphs(ui.self, request, &menu, 1, sizeof(KrMenu));
  else
    ui.show_menu(ui.self, request, &menu, 1, sizeof(KrMenu));
}

KrMenuItem Command(std::int32_t id, std::string_view label) {
  return {.struct_size = sizeof(KrMenuItem),
          .command_id = id,
          .type = KR_MENU_ITEM_COMMAND,
          .flags = KR_MENU_ITEM_ENABLED,
          .label = View(label),
          .submenu = -1};
}

template <typename Callback>
void Boundary(Callback&& callback) noexcept {
  try {
    std::forward<Callback>(callback)();
  } catch (...) {
  }
}
}  // namespace

void MockBrowser::InitLibrary() {
  library_ = std::make_unique<LibraryFixture>();
  auto& fixture = *library_;
  const auto image = [](std::uint64_t n) { return kLibraryImageBase + n; };
  fixture.downloads = {
      {1, KR_DOWNLOAD_COMPLETE, KR_DOWNLOAD_FILE_EXISTS, 1, "Krypton-Setup.exe",
       "84.2 MB • krypton.app • Yesterday", image(1)},
      {2, KR_DOWNLOAD_COMPLETE, KR_DOWNLOAD_FILE_EXISTS, 1, "annual-report.pdf",
       "2.4 MB • example.com • Today", image(2)},
      {3, KR_DOWNLOAD_COMPLETE, KR_DOWNLOAD_FILE_EXISTS, 1, "aurora.jpg",
       "6.1 MB • photos.example • Today", image(3)},
      {4, KR_DOWNLOAD_IN_PROGRESS, KR_DOWNLOAD_PROGRESS_KNOWN, 0.42f,
       "dataset.zip", "12.6 of 30.0 MB • 3.2 MB/s", image(4)},
  };
  fixture.history = {
      {1, "Krypton — Fast, private browsing", "https://krypton.app", "Today",
       0xFF7C5CFF},
      {2, "Example Domain", "https://example.com", "Today", 0xFF3B82F6},
      {3, "The Rust Programming Language", "https://doc.rust-lang.org/book",
       "Today", 0xFFDE6B35},
      {4, "WinUI 3 Gallery", "https://learn.microsoft.com/windows/apps",
       "Yesterday", 0xFF0EA5E9},
      {5, "Design tokens", "https://design.example/tokens", "Yesterday",
       0xFF10B981},
  };
  fixture.media_flags = KR_MEDIA_PLAYING | KR_MEDIA_CAN_PLAY_PAUSE |
                        KR_MEDIA_CAN_PREVIOUS | KR_MEDIA_CAN_NEXT |
                        KR_MEDIA_CAN_SEEK | KR_MEDIA_CAN_PICTURE_IN_PICTURE;
  fixture.media_present = true;

  table_.download_command = [](void* self, KrDownloadId id,
                               std::uint32_t command) noexcept {
    Boundary([&] {
      auto& mock = Self(self);
      auto& downloads = mock.library_->downloads;
      const auto it =
          std::ranges::find(downloads, id, &LibraryFixture::Download::id);
      if (it == downloads.end())
        return;
      if (command == KR_DOWNLOAD_CANCEL &&
          it->state == KR_DOWNLOAD_IN_PROGRESS) {
        it->state = KR_DOWNLOAD_CANCELLED;
        it->flags &= ~(KR_DOWNLOAD_PROGRESS_KNOWN | KR_DOWNLOAD_OPEN_WHEN_DONE);
        it->status = "Canceled";
      } else if (command == KR_DOWNLOAD_OPEN &&
                 it->state == KR_DOWNLOAD_IN_PROGRESS) {
        it->flags ^= KR_DOWNLOAD_OPEN_WHEN_DONE;
      } else {
        mock.Log(KR_LOG_INFO, command == KR_DOWNLOAD_SHOW_IN_FOLDER
                                  ? "Fixture: show download in folder"
                                  : "Fixture: open download");
        return;
      }
      mock.PushDownload(static_cast<std::size_t>(it - downloads.begin()));
    });
  };
  table_.media_command = [](void* self, KrMediaId,
                            std::uint32_t command) noexcept {
    Boundary([&] {
      auto& mock = Self(self);
      auto& fixture = *mock.library_;
      if (!fixture.media_present)
        return;
      switch (command) {
        case KR_MEDIA_PLAY_PAUSE:
          fixture.media_flags ^= KR_MEDIA_PLAYING;
          break;
        case KR_MEDIA_NEXT:
        case KR_MEDIA_PREVIOUS:
          fixture.track = (fixture.track + (command == KR_MEDIA_NEXT
                                                ? std::size_t{1}
                                                : kTracks.size() - 1)) %
                          kTracks.size();
          fixture.position = 0;
          break;
        case KR_MEDIA_TOGGLE_MUTE:
          fixture.media_flags ^= KR_MEDIA_MUTED;
          break;
        case KR_MEDIA_FOCUS:
          mock.Activate(fixture.media_tab);
          return;
        case KR_MEDIA_CLOSE:
          fixture.media_present = false;
          if (mock.attached_ &&
              KR_HAS_SLOT(&mock.ui_, KrShellUI, remove_media_session) &&
              mock.ui_.remove_media_session)
            (void)mock.ui_.remove_media_session(mock.ui_.self, 1);
          return;
        default:
          mock.Log(KR_LOG_INFO, "Fixture: media command");
          return;
      }
      mock.PushMediaSession();
    });
  };
  table_.media_seek = [](void* self, KrMediaId, double seconds) noexcept {
    Boundary([&] {
      auto& mock = Self(self);
      mock.library_->position = seconds;
      mock.PushMediaSession();
    });
  };
  table_.library_visibility_changed = [](void* self, std::uint32_t section,
                                         bool visible) noexcept {
    Boundary([&] {
      auto& mock = Self(self);
      if (section >= mock.library_->visible.size())
        return;
      mock.library_->visible[section] = visible;
      if (visible)
        mock.PushLibrarySection(section);
    });
  };
  table_.library_command = [](void* self, std::uint32_t section,
                              KrLibraryItemId item,
                              std::uint32_t command) noexcept {
    Boundary([&] {
      auto& mock = Self(self);
      auto& fixture = *mock.library_;
      if (section == KR_LIBRARY_MEDIA && command == KR_LIBRARY_ENABLE) {
        fixture.media_enabled = true;
        mock.PushLibrarySection(section);
      } else if (section == KR_LIBRARY_HISTORY && command == KR_LIBRARY_OPEN) {
        const auto it = std::ranges::find(fixture.history, item,
                                          &LibraryFixture::HistoryEntry::id);
        if (it != fixture.history.end())
          mock.OpenTab(it->url, KR_OPEN_NEW_TAB);
      } else if (section == KR_LIBRARY_HISTORY &&
                 command == KR_LIBRARY_REMOVE) {
        std::erase_if(fixture.history,
                      [item](const auto& entry) { return entry.id == item; });
        if (mock.attached_ &&
            KR_HAS_SLOT(&mock.ui_, KrShellUI, remove_library_item) &&
            mock.ui_.remove_library_item)
          (void)mock.ui_.remove_library_item(mock.ui_.self, item);
      } else {
        mock.Log(KR_LOG_INFO, "Fixture: open library media");
      }
    });
  };
}

void MockBrowser::PushDownload(std::size_t index) const {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, upsert_download) ||
      !ui_.upsert_download)
    return;
  const auto& item = library_->downloads[index];
  const KrDownload download{sizeof(KrDownload), item.id,       item.state,
                            item.flags,         item.progress, View(item.name),
                            View(item.status),  item.icon};
  (void)ui_.upsert_download(ui_.self, &download);
}

void MockBrowser::PushMediaSession() const {
  const auto& fixture = *library_;
  if (!attached_ || !fixture.media_present || !fixture.media_tab ||
      !KR_HAS_SLOT(&ui_, KrShellUI, upsert_media_session) ||
      !ui_.upsert_media_session)
    return;
  const auto& track = kTracks[fixture.track];
  const KrMediaSession session{sizeof(KrMediaSession),
                               1,
                               fixture.media_tab,
                               fixture.media_flags,
                               View(track.title),
                               View(track.artist),
                               0,
                               std::min(fixture.position, track.duration),
                               track.duration,
                               1};
  (void)ui_.upsert_media_session(ui_.self, &session);
}

void MockBrowser::PushLibrary() const {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, show_library) ||
      !ui_.upsert_download)
    return;
  const std::array<std::pair<std::uint32_t, std::uint32_t>, 4> icons{{
      {0xFF2563EB, 0xFF60A5FA},
      {0xFFDC2626, 0xFFF87171},
      {0xFF0B3D3A, 0xFF5EE3B5},
      {0xFF6B7280, 0xFFD1D5DB},
  }};
  for (std::size_t index = 0; index < library_->downloads.size(); ++index) {
    const auto& [from, to] = icons[index % icons.size()];
    const auto pixels = Picture(40, 40, from, to);
    const KrImage image{
        sizeof(KrImage), library_->downloads[index].icon, 40, 40, 40 * 4,
        pixels.data()};
    ui_.put_image(ui_.self, &image);
    PushDownload(index);
  }
  // The media plays in a background tab of the first space, so its card
  // shows in the sidebar from the start.
  auto& fixture = *library_;
  for (const auto& tab : tabs_)
    if (tab.id != active_tab_ && !(tab.flags & KR_TAB_FAVORITE)) {
      fixture.media_tab = tab.id;
      break;
    }
  PushMediaSession();
  const KrLibrarySectionState media{
      sizeof(KrLibrarySectionState), KR_LIBRARY_MEDIA,
      fixture.media_enabled ? std::uint32_t{KR_LIBRARY_SECTION_ENABLED} : 0u};
  (void)ui_.set_library_section(ui_.self, &media);
}

void MockBrowser::PushLibrarySection(std::uint32_t section) const {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, show_library))
    return;
  const auto& fixture = *library_;
  if (section == KR_LIBRARY_MEDIA) {
    const KrLibrarySectionState state{
        sizeof(KrLibrarySectionState), section,
        fixture.media_enabled ? std::uint32_t{KR_LIBRARY_SECTION_ENABLED} : 0u};
    (void)ui_.set_library_section(ui_.self, &state);
    if (!fixture.media_enabled)
      return;
    for (std::size_t index = 0; index < kMediaFiles.size(); ++index) {
      const auto& file = kMediaFiles[index];
      const auto id = kLibraryImageBase + 100 + index;
      const auto pixels = Picture(file.width, file.height, file.from, file.to);
      const KrImage image{sizeof(KrImage), id,
                          file.width,      file.height,
                          file.width * 4,  pixels.data()};
      ui_.put_image(ui_.self, &image);
      const KrLibraryItem item{
          sizeof(KrLibraryItem),
          100 + index,
          section,
          file.video ? std::uint32_t{KR_LIBRARY_ITEM_VIDEO} : 0u,
          View(file.name),
          {},
          {},
          id};
      (void)ui_.upsert_library_item(ui_.self, &item, 0);
    }
  } else if (section == KR_LIBRARY_HISTORY) {
    for (const auto& entry : fixture.history) {
      const auto id = kLibraryImageBase + 200 + entry.id;
      const auto pixels =
          Picture(32, 32, entry.color, entry.color | 0x00404040);
      const KrImage image{sizeof(KrImage), id, 32, 32, 32 * 4, pixels.data()};
      ui_.put_image(ui_.self, &image);
      const KrLibraryItem item{
          sizeof(KrLibraryItem), entry.id,        section,           0,
          View(entry.title),     View(entry.url), View(entry.group), id};
      (void)ui_.upsert_library_item(ui_.self, &item, 0);
    }
  }
}

bool MockBrowser::ShowLibraryMenu(std::uint32_t request,
                                  std::uint32_t kind,
                                  std::uint64_t target) {
  if (!attached_ || (kind != KR_MENU_DOWNLOAD && kind != KR_MENU_LIBRARY_ITEM))
    return false;
  auto& fixture = *library_;
  fixture.menu_kind = kind;
  fixture.menu_target = target;
  std::vector<KrMenuItem> items;
  const KrMenuItem separator{.struct_size = sizeof(KrMenuItem),
                             .type = KR_MENU_ITEM_SEPARATOR,
                             .submenu = -1};
  if (kind == KR_MENU_DOWNLOAD) {
    const auto it = std::ranges::find(fixture.downloads, target,
                                      &LibraryFixture::Download::id);
    if (it == fixture.downloads.end())
      return true;
    if (it->state == KR_DOWNLOAD_IN_PROGRESS) {
      items = {Command(kCancelDownload, "Cancel")};
    } else {
      if (it->flags & KR_DOWNLOAD_FILE_EXISTS)
        items = {Command(kShowInFolder, "Show in Folder"), separator,
                 Command(kDeleteFile, "Delete")};
      items.push_back(Command(kRemoveDownload, "Remove From History"));
    }
  } else if (target >= 100 && target < 100 + kMediaFiles.size()) {
    items = {Command(kOpenMedia, "Open")};
  } else {
    items = {Command(kReopenPage, "Reopen Page"),
             Command(kForgetPage, "Forget This Page")};
  }
  Deliver(ui_, request, items);
  return true;
}

bool MockBrowser::RunLibraryCommand(std::int32_t command) {
  if (command < kCommandBase || command > kOpenMedia)
    return false;
  auto& fixture = *library_;
  const auto target = fixture.menu_target;
  switch (command) {
    case kCancelDownload:
      table_.download_command(this, target, KR_DOWNLOAD_CANCEL);
      break;
    case kShowInFolder:
      table_.download_command(this, target, KR_DOWNLOAD_SHOW_IN_FOLDER);
      break;
    case kDeleteFile:
    case kRemoveDownload: {
      const auto it = std::ranges::find(fixture.downloads, target,
                                        &LibraryFixture::Download::id);
      if (it == fixture.downloads.end())
        break;
      if (command == kDeleteFile) {
        it->flags &= ~KR_DOWNLOAD_FILE_EXISTS;
        it->status = "File deleted";
        PushDownload(static_cast<std::size_t>(it - fixture.downloads.begin()));
      } else {
        fixture.downloads.erase(it);
        if (attached_ && KR_HAS_SLOT(&ui_, KrShellUI, remove_download) &&
            ui_.remove_download)
          (void)ui_.remove_download(ui_.self, target);
      }
      break;
    }
    case kReopenPage:
      table_.library_command(this, KR_LIBRARY_HISTORY, target, KR_LIBRARY_OPEN);
      break;
    case kForgetPage:
      table_.library_command(this, KR_LIBRARY_HISTORY, target,
                             KR_LIBRARY_REMOVE);
      break;
    default:
      Log(KR_LOG_INFO, "Fixture: open library media");
      break;
  }
  return true;
}
}  // namespace kr::fixtures
