#include "krypton/ui/model/library.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kr {
namespace {
[[nodiscard]] bool Finite(double value) noexcept {
  return std::isfinite(value);
}
}  // namespace

const Download* LibraryState::FindDownload(DownloadId id) const noexcept {
  const auto it = std::ranges::find(downloads, id, &Download::id);
  return it == downloads.end() ? nullptr : &*it;
}

Result<void> LibraryState::UpsertDownload(Download value) {
  if (!value.id || value.state > DownloadState::kFailed ||
      !std::isfinite(value.progress))
    return Fail(ErrorCode::kInvalidArgument, "Invalid download");
  value.progress = std::clamp(value.progress, 0.0f, 1.0f);
  const auto it = std::ranges::find(downloads, value.id, &Download::id);
  if (value.flags.Has(DownloadFlag::kStarted) &&
      (it == downloads.end() || !it->flags.Has(DownloadFlag::kStarted)))
    ++download_start_serial;
  if (it == downloads.end())
    downloads.push_back(std::move(value));
  else
    *it = std::move(value);
  ++revision;
  return {};
}

Result<void> LibraryState::RemoveDownload(DownloadId id) {
  if (!std::erase_if(downloads,
                     [id](const Download& value) { return value.id == id; }))
    return Fail(ErrorCode::kNotFound, "Unknown download");
  ++revision;
  return {};
}

LibraryState::MediaEntry* LibraryState::FindMedia(MediaId id) noexcept {
  const auto it = std::ranges::find_if(
      media, [id](const MediaEntry& entry) { return entry.value.id == id; });
  return it == media.end() ? nullptr : &*it;
}

const LibraryState::MediaEntry* LibraryState::FindMedia(
    MediaId id) const noexcept {
  const auto it = std::ranges::find_if(
      media, [id](const MediaEntry& entry) { return entry.value.id == id; });
  return it == media.end() ? nullptr : &*it;
}

Result<void> LibraryState::UpsertMedia(MediaSession value) {
  if (!value.id || !value.tab || !Finite(value.position) ||
      !Finite(value.duration) || !Finite(value.playback_rate) ||
      value.duration < 0)
    return Fail(ErrorCode::kInvalidArgument, "Invalid media session");
  value.position = std::clamp(value.position, 0.0, value.duration);
  if (auto* entry = FindMedia(value.id)) {
    entry->value = std::move(value);
    if (entry->value.flags.Has(MediaFlag::kSharing)) {
      entry->revealed = true;
      entry->reveal_pending = false;
    } else if (!entry->revealed &&
               !entry->value.flags.Has(MediaFlag::kPlaying)) {
      entry->reveal_pending = false;
    }
  } else {
    const bool sharing = value.flags.Has(MediaFlag::kSharing);
    media.push_back({std::move(value), sharing, false});
  }
  ++revision;
  return {};
}

Result<void> LibraryState::RemoveMedia(MediaId id) {
  if (!std::erase_if(media, [id](const MediaEntry& entry) {
        return entry.value.id == id;
      }))
    return Fail(ErrorCode::kNotFound, "Unknown media session");
  ++revision;
  return {};
}

Result<void> LibraryState::SetSection(LibrarySection which,
                                      Flags<LibrarySectionFlag> flags) {
  if (SectionIndex(which) >= kLibrarySectionCount)
    return Fail(ErrorCode::kInvalidArgument, "Unknown library section");
  sections[SectionIndex(which)] = flags;
  if (open && Hidden(section))
    Open(std::nullopt);
  ++revision;
  return {};
}

const LibraryItem* LibraryState::FindItem(LibraryItemId id) const noexcept {
  const auto known = item_sections_.find(id);
  if (known == item_sections_.end())
    return nullptr;
  const auto& list = items[SectionIndex(known->second)];
  const auto it = std::ranges::find(list, id, &LibraryItem::id);
  return it == list.end() ? nullptr : &*it;
}

Result<void> LibraryState::UpsertItem(LibraryItem value, LibraryItemId before) {
  if (!value.id || (value.section != LibrarySection::kMedia &&
                    value.section != LibrarySection::kBoosts &&
                    value.section != LibrarySection::kHistory &&
                    value.section != LibrarySection::kRouting))
    return Fail(ErrorCode::kInvalidArgument, "Invalid library item");
  const auto known = item_sections_.find(value.id);
  if (known != item_sections_.end() && known->second != value.section)
    return Fail(ErrorCode::kInvalidArgument, "Library item changed section");
  auto& list = items[SectionIndex(value.section)];
  if (known != item_sections_.end()) {
    *std::ranges::find(list, value.id, &LibraryItem::id) = std::move(value);
    ++revision;
    return {};
  }
  auto at = list.end();
  if (before) {
    at = std::ranges::find(list, before, &LibraryItem::id);
    if (at == list.end())
      return Fail(ErrorCode::kNotFound, "Unknown library item");
  }
  item_sections_.emplace(value.id, value.section);
  list.insert(at, std::move(value));
  ++revision;
  return {};
}

Result<void> LibraryState::RemoveItem(LibraryItemId id) {
  const auto known = item_sections_.find(id);
  if (known == item_sections_.end())
    return Fail(ErrorCode::kNotFound, "Unknown library item");
  std::erase_if(items[SectionIndex(known->second)],
                [id](const LibraryItem& value) { return value.id == id; });
  item_sections_.erase(known);
  ++revision;
  return {};
}

bool LibraryState::Hidden(LibrarySection which) const noexcept {
  return SectionIndex(which) >= kLibrarySectionCount ||
         sections[SectionIndex(which)].Has(LibrarySectionFlag::kHidden);
}

void LibraryState::Open(std::optional<LibrarySection> requested) {
  if (requested && *requested != LibrarySection::kRouting)
    routing_editor.open = false;
  if (requested && SectionIndex(*requested) < kLibrarySectionCount &&
      !Hidden(*requested))
    section = *requested;
  if (Hidden(section)) {
    // Zen falls back to history, the one section every window has.
    section = LibrarySection::kHistory;
    for (std::size_t index = 0; Hidden(section) && index < kLibrarySectionCount;
         ++index)
      section = static_cast<LibrarySection>(index);
  }
  open = !Hidden(section);
  ++revision;
}

void LibraryState::Close() noexcept {
  if (!open)
    return;
  open = false;
  routing_editor.open = false;
  ++revision;
}
}  // namespace kr
