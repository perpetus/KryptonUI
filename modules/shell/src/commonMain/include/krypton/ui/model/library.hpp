#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "krypton/ui/core/flags.hpp"
#include "krypton/ui/core/ids.hpp"
#include "krypton/ui/core/result.hpp"

// Downloads, sidebar media sessions and the library panel, following Zen's
// ZenLibraryWidget.sys.mjs, ZenMediaController.mjs and ZenLibrary.mjs.
namespace kr {
using DownloadId = Id<struct DownloadTag>;
using MediaId = Id<struct MediaTag>;
using LibraryItemId = Id<struct LibraryItemTag>;

enum class DownloadState : std::uint32_t {
  kInProgress,
  kPaused,
  kComplete,
  kCancelled,
  kFailed
};
enum class DownloadFlag : std::uint32_t {
  kProgressKnown = 1u << 0,
  kFileExists = 1u << 1,
  kOpenWhenDone = 1u << 2,
  kBlocked = 1u << 3,
  kStarted = 1u << 4,
};
template <>
inline constexpr bool kIsFlagEnum<DownloadFlag> = true;
enum class DownloadCommand : std::uint32_t { kOpen, kCancel, kShowInFolder };

struct Download {
  DownloadId id;
  DownloadState state = DownloadState::kInProgress;
  Flags<DownloadFlag> flags;
  float progress = 0;
  std::string file_name, status;
  ImageId icon;

  // Zen's #isPending: still coming in, or stopped with resumable data.
  [[nodiscard]] bool Pending() const noexcept {
    return state == DownloadState::kInProgress ||
           state == DownloadState::kPaused;
  }
};

enum class MediaFlag : std::uint32_t {
  kPlaying = 1u << 0,
  kMuted = 1u << 1,
  kCanPlayPause = 1u << 2,
  kCanPrevious = 1u << 3,
  kCanNext = 1u << 4,
  kCanSeek = 1u << 5,
  kCanPictureInPicture = 1u << 6,
  kPictureInPicture = 1u << 7,
  kSharing = 1u << 8,
  kMicrophoneMuted = 1u << 9,
  kCameraMuted = 1u << 10,
  kCanToggleMicrophone = 1u << 11,
  kCanToggleCamera = 1u << 12
};
template <>
inline constexpr bool kIsFlagEnum<MediaFlag> = true;
enum class MediaCommand : std::uint32_t {
  kPlayPause,
  kPrevious,
  kNext,
  kToggleMute,
  kTogglePictureInPicture,
  kClose,
  kFocus,
  kToggleMicrophone,
  kToggleCamera
};

struct MediaSession {
  MediaId id;
  TabId tab;
  Flags<MediaFlag> flags;
  std::string title, artist;
  ImageId icon;
  double position = 0, duration = 0, playback_rate = 1;
};

enum class LibrarySection : std::uint32_t {
  kMedia,
  kDownloads,
  kBoosts,
  kSpaces,
  kHistory,
  kRouting
};
inline constexpr std::size_t kLibrarySectionCount = 6;
enum class LibrarySectionFlag : std::uint32_t {
  kHidden = 1u << 0,
  kEnabled = 1u << 1,
  kLoading = 1u << 2
};
template <>
inline constexpr bool kIsFlagEnum<LibrarySectionFlag> = true;
enum class LibraryItemFlag : std::uint32_t {
  kVideo = 1u << 0,
  kFile = 1u << 1,
  kEnabled = 1u << 2
};
template <>
inline constexpr bool kIsFlagEnum<LibraryItemFlag> = true;
enum class LibraryCommand : std::uint32_t {
  kOpen,
  kEnable,
  kToggle,
  kRemove,
  kCreate
};

struct LibraryItem {
  LibraryItemId id;
  LibrarySection section = LibrarySection::kHistory;
  Flags<LibraryItemFlag> flags;
  std::string title, subtitle, group;
  ImageId image;
};

enum class RoutingMatch : std::uint32_t { kDomain, kContains, kExact, kRegex };
struct RoutingDraft {
  std::uint64_t id = 0, revision = 0;
  std::uint32_t serial = 0;
  std::string pattern;
  SpaceId workspace;
  RoutingMatch match = RoutingMatch::kContains;
  bool enabled = true, new_tabs_only = false;
};
struct RoutingEditorState {
  bool open = false, error = false;
  RoutingDraft draft;
};

// Host-published records plus the panel's UI-local state. The host owns every
// record; the panel's open state and section belong to the UI.
struct LibraryState {
  LibraryState() noexcept {
    sections[static_cast<std::size_t>(LibrarySection::kRouting)].Set(
        LibrarySectionFlag::kHidden);
  }
  [[nodiscard]] Result<void> UpsertDownload(Download value);
  [[nodiscard]] Result<void> RemoveDownload(DownloadId id);
  // Media cards start hidden and are revealed by the Shell once playback has
  // lasted, so short sounds never show one. Sharing cards show at once.
  [[nodiscard]] Result<void> UpsertMedia(MediaSession value);
  [[nodiscard]] Result<void> RemoveMedia(MediaId id);
  [[nodiscard]] Result<void> SetSection(LibrarySection which,
                                        Flags<LibrarySectionFlag> flags);
  [[nodiscard]] Result<void> UpsertItem(LibraryItem value,
                                        LibraryItemId before);
  [[nodiscard]] Result<void> RemoveItem(LibraryItemId id);
  [[nodiscard]] const Download* FindDownload(DownloadId id) const noexcept;
  [[nodiscard]] const LibraryItem* FindItem(LibraryItemId id) const noexcept;
  [[nodiscard]] bool Hidden(LibrarySection which) const noexcept;
  // Opens on `section`, or on the current one when it is hidden or absent.
  void Open(std::optional<LibrarySection> section);
  void Close() noexcept;

  struct MediaEntry {
    MediaSession value;
    bool revealed = false;
    bool reveal_pending = false;
    // Captures the state revision when this reveal was scheduled.
    std::uint64_t reveal_generation = 0;
  };
  [[nodiscard]] MediaEntry* FindMedia(MediaId id) noexcept;
  [[nodiscard]] const MediaEntry* FindMedia(MediaId id) const noexcept;

  std::vector<Download> downloads;  // host order, newest last
  std::vector<MediaEntry> media;    // host order, newest last
  std::array<std::vector<LibraryItem>, kLibrarySectionCount> items;
  std::array<Flags<LibrarySectionFlag>, kLibrarySectionCount> sections{};
  bool open = false;
  LibrarySection section = LibrarySection::kHistory;
  std::uint64_t revision = 0;
  std::string routing_query;
  RoutingEditorState routing_editor;
  std::uint64_t download_start_serial = 0;

 private:
  std::unordered_map<LibraryItemId, LibrarySection> item_sections_;
};

[[nodiscard]] constexpr std::size_t SectionIndex(
    LibrarySection section) noexcept {
  return static_cast<std::size_t>(section);
}
}  // namespace kr
