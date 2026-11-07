#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/shell/shell.hpp"

namespace kr {
bool BrowserPort::SaveRoutingRule(const RoutingDraft& value) const {
  if (!KR_HAS_SLOT(&table_, KrBrowser, save_routing_rule) ||
      !table_.save_routing_rule)
    return false;
  const KrRoutingRule rule{
      sizeof(rule),          value.id,
      value.revision,        {value.pattern.data(), value.pattern.size()},
      value.workspace.value, std::to_underlying(value.match),
      value.enabled,         value.new_tabs_only};
  return table_.save_routing_rule(table_.self, &rule) == KR_OK;
}

Result<void> Shell::ShowRoutingEditor(RoutingDraft value) {
  if (OnboardingActive() || store_.library().Hidden(LibrarySection::kRouting) ||
      value.pattern.size() > 2048 || value.match > RoutingMatch::kRegex ||
      (value.workspace && !store_.FindSpace(value.workspace)))
    return Fail(ErrorCode::kInvalidArgument, "Routing editing is unavailable");
  DismissBoostEditor();
  CloseAddress();
  OnWorkspaceEditorDismissed();
  OnThemeEditorDismissed();
  if (store_.ui().settings.open)
    OnSettingsDismissed();
  OnTabPreviewDismissed(store_.ui().tab_preview.request_id);
  if (!menu_anchors_.empty())
    DismissMenu(menu_anchors_.begin()->first);
  auto& library = store_.EditLibrary();
  value.serial = library.routing_editor.draft.serial + 1;
  library.routing_editor = {true, false, std::move(value)};
  library.Open(LibrarySection::kRouting);
  NotifyLibraryVisibility();
  ScheduleRender();
  return {};
}

bool Shell::OnRoutingSubmitted(const RoutingDraft& value) {
  const auto& state = store_.library();
  const auto& editor = state.routing_editor;
  if (!state.open || state.section != LibrarySection::kRouting ||
      !editor.open || value.serial != editor.draft.serial ||
      value.id != editor.draft.id || value.revision != editor.draft.revision)
    return false;
  const bool valid = !value.pattern.empty() && value.pattern.size() <= 2048 &&
                     value.match <= RoutingMatch::kRegex &&
                     (!value.workspace || store_.FindSpace(value.workspace));
  const bool saved = valid && browser_.SaveRoutingRule(value);
  auto& updated = store_.EditLibrary();
  if (saved)
    updated.routing_editor.open = false;
  updated.routing_editor.error = !saved;
  ++updated.revision;
  ScheduleRender();
  return saved;
}

void Shell::OnRoutingCancelled() {
  auto& library = store_.EditLibrary();
  library.routing_editor.open = false;
  ++library.revision;
  ScheduleRender();
}

void BrowserPort::RefreshDownloads() const {
  if (KR_HAS_SLOT(&table_, KrBrowser, refresh_downloads) &&
      table_.refresh_downloads)
    table_.refresh_downloads(table_.self);
}
void Shell::OnDownloadStackOpened() {
  browser_.RefreshDownloads();
}

void BrowserPort::SendDownloadCommand(DownloadId id,
                                      DownloadCommand command) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, download_command) &&
      table_.download_command)
    table_.download_command(table_.self, id.value, std::to_underlying(command));
}
void BrowserPort::SendMediaCommand(MediaId id, MediaCommand command) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, media_command) && table_.media_command)
    table_.media_command(table_.self, id.value, std::to_underlying(command));
}
bool BrowserPort::CanSeekMedia() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, media_seek) && table_.media_seek;
}
void BrowserPort::SeekMedia(MediaId id, double seconds) const {
  if (CanSeekMedia())
    table_.media_seek(table_.self, id.value, seconds);
}
void BrowserPort::LibraryVisibilityChanged(LibrarySection section,
                                           bool visible) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, library_visibility_changed) &&
      table_.library_visibility_changed)
    table_.library_visibility_changed(table_.self, std::to_underlying(section),
                                      visible);
}
void BrowserPort::SendLibraryCommand(LibrarySection section,
                                     LibraryItemId item,
                                     LibraryCommand command) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, library_command) &&
      table_.library_command)
    table_.library_command(table_.self, std::to_underlying(section), item.value,
                           std::to_underlying(command));
}

Result<void> Shell::UpsertDownload(Download value) {
  auto result = store_.EditLibrary().UpsertDownload(std::move(value));
  if (result)
    ScheduleRender();
  return result;
}

Result<void> Shell::RemoveDownload(DownloadId id) {
  auto result = store_.EditLibrary().RemoveDownload(id);
  if (result)
    ScheduleRender();
  return result;
}

Result<void> Shell::UpsertMediaSession(MediaSession value) {
  const auto id = value.id;
  auto result = store_.EditLibrary().UpsertMedia(std::move(value));
  if (!result)
    return result;
  RevealMediaLater(id);
  ScheduleRender();
  return result;
}

Result<void> Shell::RemoveMediaSession(MediaId id) {
  auto result = store_.EditLibrary().RemoveMedia(id);
  if (result)
    ScheduleRender();
  return result;
}

// Zen creates a card as playback starts but shows it only if the media still
// plays a moment later, so notification pings never reveal one.
void Shell::RevealMediaLater(MediaId id) {
  auto* entry = store_.EditLibrary().FindMedia(id);
  if (!entry || entry->revealed || entry->reveal_pending ||
      !entry->value.flags.Has(MediaFlag::kPlaying) || !view_)
    return;
  entry->reveal_pending = true;
  const auto generation = store_.library().revision;
  entry->reveal_generation = generation;
  view_->PostDelayedTask(
      [this, alive = std::weak_ptr<int>(lifetime_), id, generation] {
        if (alive.expired())
          return;
        const auto* pending = store_.library().FindMedia(id);
        if (!pending || !pending->reveal_pending ||
            pending->reveal_generation != generation)
          return;
        auto* entry = store_.EditLibrary().FindMedia(id);
        entry->reveal_pending = false;
        entry->revealed = entry->value.flags.Has(MediaFlag::kPlaying);
        ScheduleRender();
      },
      std::chrono::milliseconds(
          static_cast<int>(design::kTokens.media_controls.reveal_delay_ms)));
}

Result<void> Shell::SetLibrarySection(LibrarySection section,
                                      Flags<LibrarySectionFlag> flags) {
  auto result = store_.EditLibrary().SetSection(section, flags);
  if (result) {
    NotifyLibraryVisibility();
    ScheduleRender();
  }
  return result;
}

Result<void> Shell::UpsertLibraryItem(LibraryItem value, LibraryItemId before) {
  auto result = store_.EditLibrary().UpsertItem(std::move(value), before);
  if (result)
    ScheduleRender();
  return result;
}

Result<void> Shell::RemoveLibraryItem(LibraryItemId id) {
  auto result = store_.EditLibrary().RemoveItem(id);
  if (result)
    ScheduleRender();
  return result;
}

Result<void> Shell::ShowLibrary(LibrarySection section) {
  DismissBoostEditor();
  if (OnboardingActive())
    return Fail(ErrorCode::kInvalidArgument, "Onboarding is showing");
  if (SectionIndex(section) >= kLibrarySectionCount)
    return Fail(ErrorCode::kInvalidArgument, "Unknown library section");
  if (store_.library().Hidden(section))
    return Fail(ErrorCode::kInvalidArgument, "Library section is hidden");
  store_.EditLibrary().Open(section);
  NotifyLibraryVisibility();
  ScheduleRender();
  return {};
}

void Shell::NotifyLibraryVisibility() {
  const auto& state = store_.library();
  const auto visible = state.open ? std::optional(state.section) : std::nullopt;
  if (visible == library_visible_)
    return;
  if (library_visible_)
    browser_.LibraryVisibilityChanged(*library_visible_, false);
  library_visible_ = visible;
  if (visible)
    browser_.LibraryVisibilityChanged(*visible, true);
}

void Shell::OnDownloadCommand(DownloadId id, DownloadCommand command) {
  if (store_.library().FindDownload(id))
    browser_.SendDownloadCommand(id, command);
}

void Shell::OnMediaCommand(MediaId id, MediaCommand command) {
  const auto* entry = store_.library().FindMedia(id);
  if (!entry)
    return;
  const auto has = [&](MediaFlag flag) { return entry->value.flags.Has(flag); };
  switch (command) {
    case MediaCommand::kPlayPause:
      if (!has(MediaFlag::kCanPlayPause))
        return;
      break;
    case MediaCommand::kPrevious:
      if (!has(MediaFlag::kCanPrevious))
        return;
      break;
    case MediaCommand::kNext:
      if (!has(MediaFlag::kCanNext))
        return;
      break;
    case MediaCommand::kTogglePictureInPicture:
      if (!has(MediaFlag::kCanPictureInPicture))
        return;
      break;
    case MediaCommand::kToggleMicrophone:
      if (!has(MediaFlag::kCanToggleMicrophone))
        return;
      break;
    case MediaCommand::kToggleCamera:
      if (!has(MediaFlag::kCanToggleCamera))
        return;
      break;
    case MediaCommand::kToggleMute:
    case MediaCommand::kClose:
    case MediaCommand::kFocus:
      break;
    default:
      return;
  }
  browser_.SendMediaCommand(id, command);
}

void Shell::OnMediaSeek(MediaId id, double seconds) {
  const auto* entry = store_.library().FindMedia(id);
  if (entry && std::isfinite(seconds) &&
      entry->value.flags.Has(MediaFlag::kCanSeek))
    browser_.SeekMedia(id, std::clamp(seconds, 0.0, entry->value.duration));
}

// Zen's ZenLibrary.toggle(): plainly toggles open and closed.
void Shell::OnLibraryToggled() {
  if (OnboardingActive())
    return;
  if (store_.library().open)
    store_.EditLibrary().Close();
  else
    store_.EditLibrary().Open(std::nullopt);
  NotifyLibraryVisibility();
  ScheduleRender();
}

void Shell::OnLibrarySectionSelected(LibrarySection section) {
  const auto& state = store_.library();
  if (!state.open || state.section == section || state.Hidden(section))
    return;
  store_.EditLibrary().Open(section);
  NotifyLibraryVisibility();
  ScheduleRender();
}

void Shell::OnLibraryDismissed() {
  if (!store_.library().open)
    return;
  store_.EditLibrary().Close();
  NotifyLibraryVisibility();
  ScheduleRender();
}

void Shell::OnLibrarySearchChanged(std::string_view query) {
  const auto& state = store_.library();
  if (!state.open || state.section != LibrarySection::kRouting ||
      query.size() > 2048 || state.routing_query == query)
    return;
  auto& edited = store_.EditLibrary();
  edited.routing_query = query;
  ++edited.revision;
  ScheduleRender();
}

void Shell::OnLibraryCommand(LibrarySection section,
                             LibraryItemId item,
                             LibraryCommand command) {
  const auto& state = store_.library();
  if (!state.open || state.section != section)
    return;
  if (command == LibraryCommand::kCreate) {
    if (item || (section != LibrarySection::kRouting &&
                 section != LibrarySection::kBoosts))
      return;
    if (section == LibrarySection::kBoosts)
      OnLibraryDismissed();
    browser_.SendLibraryCommand(section, {}, command);
    return;
  }
  if (command == LibraryCommand::kEnable) {
    if (section == LibrarySection::kMedia &&
        !state.sections[SectionIndex(section)].Has(
            LibrarySectionFlag::kEnabled))
      browser_.SendLibraryCommand(section, {}, command);
    return;
  }
  const auto* value = state.FindItem(item);
  if (!value || value->section != section)
    return;
  browser_.SendLibraryCommand(section, item, command);
}
}  // namespace kr
