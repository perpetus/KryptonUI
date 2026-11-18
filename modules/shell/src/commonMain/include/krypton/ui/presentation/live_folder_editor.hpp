#pragma once

#include <algorithm>
#include <array>
#include <optional>

#include "krypton/ui/presentation/view_model.hpp"

namespace kr {
// Shared defaults for direct provider actions and the optional full editor.
inline constexpr std::string_view kLiveFolderGitHubSignInStatus =
    "Not signed in to GitHub.";
[[nodiscard]] constexpr bool IsGitHubLiveFolder(
    LiveFolderProvider provider) noexcept {
  return provider == LiveFolderProvider::kPullRequests ||
         provider == LiveFolderProvider::kIssues;
}
[[nodiscard]] inline bool LiveFolderNeedsSignIn(
    const FolderVm& folder) noexcept {
  return (folder.live_kind ==
              std::to_underlying(LiveFolderProvider::kPullRequests) ||
          folder.live_kind ==
              std::to_underlying(LiveFolderProvider::kIssues)) &&
         folder.live_status == kLiveFolderGitHubSignInStatus;
}
[[nodiscard]] inline std::optional<UiText> LiveFolderDefaultName(
    const FolderVm& folder) noexcept {
  if (folder.name != "Folder")
    return std::nullopt;
  switch (static_cast<LiveFolderProvider>(folder.live_kind)) {
    case LiveFolderProvider::kPullRequests:
      return UiText::kLiveFolderPullRequestsMenu;
    case LiveFolderProvider::kIssues:
      return UiText::kLiveFolderIssuesMenu;
    case LiveFolderProvider::kRss:
      return UiText::kLiveFolderRssMenu;
    default:
      return std::nullopt;
  }
}
struct LiveFolderFeedback {
  std::string_view text;
  bool warning = false;
};
[[nodiscard]] inline LiveFolderFeedback PresentLiveFolderFeedback(
    const FolderVm& folder) noexcept {
  if (!folder.live_kind || folder.live_status == "Up to date.")
    return {};
  const auto text = folder.live_status;
  const bool ordinary =
      text.empty() || text == "Refreshing..." || text == "Not refreshed yet." ||
      text.starts_with("No matching ") || text == "No feed entries.";
  return {text, !ordinary};
}
struct LiveFolderGitHubFilter {
  UiText label;
  std::string_view source;
  bool pulls_only = false;
};
inline constexpr std::array kLiveFolderGitHubFilters{
    LiveFolderGitHubFilter{UiText::kLiveFolderAssigned, "assignee:@me"},
    LiveFolderGitHubFilter{UiText::kLiveFolderCreated, "author:@me"},
    LiveFolderGitHubFilter{UiText::kLiveFolderReviewRequested,
                           "user-review-requested:@me", true},
    LiveFolderGitHubFilter{UiText::kLiveFolderInvolving, "involves:@me"}};

struct LiveFolderDraft {
  LiveFolderProvider provider = LiveFolderProvider::kOff;
  std::string_view source;
  unsigned interval_minutes = 30, max_items = 20;
};
[[nodiscard]] inline LiveFolderDraft PresentLiveFolderDraft(
    const FolderVm& folder,
    std::optional<LiveFolderProvider> preset) noexcept {
  const auto current =
      folder.live_kind <= std::to_underlying(LiveFolderProvider::kIssues)
          ? static_cast<LiveFolderProvider>(folder.live_kind)
          : LiveFolderProvider::kOff;
  const auto selected = preset.value_or(current);
  // PRs and issues share GitHub search syntax; a feed URL is a different input.
  const bool compatible =
      selected == current ||
      (IsGitHubLiveFolder(selected) && IsGitHubLiveFolder(current));
  return {selected,
          compatible
              ? folder.live_source
              : (IsGitHubLiveFolder(selected) ? std::string_view{"assignee:@me"}
                                              : std::string_view{}),
          folder.live_interval_minutes, folder.live_max_items};
}
struct LiveFolderFields {
  bool enabled;
  UiText source_label, source_hint;
};
[[nodiscard]] constexpr LiveFolderFields PresentLiveFolderFields(
    LiveFolderProvider provider) noexcept {
  const bool feed = provider == LiveFolderProvider::kRss;
  return {provider != LiveFolderProvider::kOff,
          feed ? UiText::kLiveFolderFeedUrl : UiText::kLiveFolderGitHubFilters,
          feed ? UiText::kLiveFolderFeedHint : UiText::kLiveFolderGitHubHint};
}
struct LiveFolderEditorMetrics {
  float width, max_height;
};
[[nodiscard]] constexpr LiveFolderEditorMetrics MeasureLiveFolderEditor(
    Rect work_area) noexcept {
  constexpr auto& t = design::kTokens.live_folder_editor;
  const auto available =
      work_area.Inset(t.margin + t.padding + design::kTokens.menu.border_width);
  return {std::max(1.0f, std::min(t.width, available.width)),
          std::max(1.0f, std::min(t.max_height, available.height))};
}
}  // namespace kr
