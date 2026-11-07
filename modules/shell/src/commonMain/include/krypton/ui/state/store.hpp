#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "krypton/ui/core/flags.hpp"
#include "krypton/ui/core/result.hpp"
#include "krypton/ui/model/address.hpp"
#include "krypton/ui/model/boost.hpp"
#include "krypton/ui/model/browser_tools.hpp"
#include "krypton/ui/model/library.hpp"
#include "krypton/ui/model/loading_progress.hpp"
#include "krypton/ui/model/model.hpp"
#include "krypton/ui/model/onboarding.hpp"
#include "krypton/ui/model/privacy_features.hpp"
#include "krypton/ui/model/settings.hpp"
#include "krypton/ui/model/ui_text.hpp"
#include "krypton/ui/model/workspace_editor.hpp"
#include "krypton/ui/model/workspace_swipe.hpp"
#include "krypton/ui/state/image_store.hpp"
#include "krypton/ui/state/multi_page_state.hpp"

namespace kr {

// What changed since the last render. Views use it to skip untouched sections.
enum class Dirty : std::uint32_t {
  kTabs = 1u << 0,
  kActiveTab = 1u << 1,
  kSpaces = 1u << 2,
  kNav = 1u << 3,
  kActions = 1u << 4,
  kTheme = 1u << 5,
  kWindow = 1u << 6,
  kLayout = 1u << 7,
  kCommandBar = 1u << 8,
  kImages = 1u << 9,
  kText = 1u << 10,
  kTabPreview = 1u << 11,
  kThemeEditor = 1u << 12,
  kLoading = 1u << 13,
  kSettings = 1u << 14,
  kWorkspaceEditor = 1u << 15,
  kBrowserTools = 1u << 16,
  kPrivacyFeatures = 1u << 17,
  kLibrary = 1u << 18,
  kOnboarding = 1u << 19,
  kMultiPage = 1u << 20,
  kAll = (1u << 21) - 1,
};
template <>
inline constexpr bool kIsFlagEnum<Dirty> = true;

// UI-local state: things the browser doesn't own, such as what's typed in the
// command bar. Browser state only changes when the host pushes it.
struct CommandBarState {
  bool open = false;
  bool pointer_open = false, typed = false;
  std::string text;
  bool select_all = false;
  AddressSelection selection;
  std::uint64_t edit_revision = 0;
  // Bumped on every open, so a view focuses its input exactly once per open.
  std::uint32_t open_serial = 0;
  std::uint32_t request_id = 0;  // suggestions we're waiting for
  std::uint64_t generation = 0;
  std::vector<Suggestion> suggestions;
  int selected = 0;
};

struct TabPreviewState {
  TabId tab;
  std::uint32_t request_id = 0;
  TabPreviewData data;
};

struct ThemeEditorState {
  bool open = false;
  bool edited = false;
  SpaceId space;
  WorkspaceTheme value;
  std::size_t page = 0;
};

struct SplitChooserState {
  bool open = false;
  std::size_t count = 0;
  std::array<TabId, kMaxSplitMembers> selected{};
  SplitLayout layout = SplitLayout::kColumns;
  bool layout_only = false;
  TabId target;
  Rect anchor;
  SplitGroupId group;
  std::uint64_t group_revision = 0;
};

struct UiState {
  SplitChooserState split_chooser;
  BoostEditorState boost_editor;
  WorkspaceEditorState workspace_editor;
  SettingsState settings;
  bool address_focused = false;
  ThemeEditorState theme_editor;
  TabPreviewState tab_preview;
  NativeTitleBarMetrics native_titlebar;
  WorkspaceSwipeState workspace_swipe;
  CommandBarState command_bar;
  bool sidebar_revealed = false;  // autohide: pointer is at the edge
  bool toolbar_revealed = false;
};

// Single owner of everything the UI knows. Applies incremental updates from
// the host, validates them, and records what's dirty. It never renders.
class Store {
 public:
  [[nodiscard]] Result<void> UpsertSplitGroup(const SplitGroup& group);
  [[nodiscard]] Result<void> RemoveSplitGroup(SplitGroupId id,
                                              std::uint64_t revision);
  [[nodiscard]] Result<void> SetGlance(const GlanceState& state);
  void OpenBoostEditor(BoostDraft value);
  void UpdateBoostEditor(BoostDraft value, bool picking);
  void CloseBoostEditor();
  [[nodiscard]] Result<void> OpenSplitChooser();
  [[nodiscard]] Result<void> OpenSplitOptions(TabId tab, Rect anchor);
  [[nodiscard]] Result<void> SetSplitSelection(std::span<const TabId> selected);
  void SetSplitChooserLayout(SplitLayout layout);
  void CloseSplitChooser();
  [[nodiscard]] const SplitGroup* SplitForTab(TabId tab) const noexcept;
  [[nodiscard]] const MultiPageState* multi_page() const noexcept {
    return multi_page_ && (!multi_page_->groups.empty() ||
                           multi_page_->glance.phase != GlancePhase::kClosed)
               ? multi_page_.get()
               : nullptr;
  }
  [[nodiscard]] TabId presented_tab() const noexcept {
    return multi_page_ ? PresentedTab(active_tab_, multi_page_->glance)
                       : active_tab_;
  }
  [[nodiscard]] bool IsTransientTab(TabId tab) const noexcept {
    return multi_page_ && multi_page_->glance.phase != GlancePhase::kClosed &&
           multi_page_->glance.identity.child == tab;
  }
  // Tabs. Order is the host's order; sections are derived by the Presenter.
  [[nodiscard]] Result<void> InsertTab(std::size_t index, Tab tab);
  [[nodiscard]] Result<void> UpsertFolder(Folder folder);
  [[nodiscard]] Result<void> RemoveFolder(FolderId id);
  [[nodiscard]] const Folder* FindFolder(FolderId id) const noexcept;
  [[nodiscard]] std::optional<std::size_t> FolderIndex(
      FolderId id) const noexcept {
    const auto found = folder_index_.find(id);
    return found == folder_index_.end() ? std::nullopt
                                        : std::optional{found->second};
  }
  [[nodiscard]] std::span<const Folder> folders() const noexcept {
    return folders_;
  }
  [[nodiscard]] Result<void> UpdateTab(Tab tab);
  [[nodiscard]] Result<void> MoveTab(TabId id, std::size_t to_index);
  [[nodiscard]] Result<void> RemoveTab(TabId id);
  [[nodiscard]] Result<void> SetActiveTab(TabId id);

  void SetSpaces(std::vector<Space> spaces, SpaceId active);
  void SetNav(NavState nav);
  [[nodiscard]] Result<void> SetLoadProgress(const LoadProgressUpdate& update);
  void SetAddressFocused(bool focused);
  void SetActions(std::vector<Action> actions);
  void SetTheme(const Theme& theme);
  [[nodiscard]] Result<void> SetWorkspaceTheme(SpaceId space,
                                               const WorkspaceTheme& theme);
  void OpenThemeEditor(SpaceId space);
  void CloseThemeEditor();
  [[nodiscard]] bool EditTheme(const ThemeEdit& edit);
  [[nodiscard]] const WorkspaceTheme* WorkspaceThemeFor(
      SpaceId space) const noexcept;
  void SetWindow(const WindowState& window);
  void SetLayoutPrefs(const LayoutPrefs& prefs);
  [[nodiscard]] Result<void> SetAppearancePrefs(
      const AppearancePrefs& prefs,
      bool icon_pack_editable = false);
  void OpenSettings(SettingsSection section,
                    bool appearance_editable,
                    bool layout_editable);
  void SetSettingsCapabilities(bool workspaces,
                               bool search,
                               bool default_browser,
                               bool permissions,
                               bool switching) {
    auto& s = ui_.settings;
    s.workspace_editable = workspaces;
    s.workspace_switchable = switching;
    s.search_editable = search;
    s.default_editable = default_browser;
    s.permissions_available = permissions;
    MarkDirty(Dirty::kSettings);
  }
  void CloseSettings();
  void SelectSettingsSection(SettingsSection section);
  [[nodiscard]] const AppearancePrefs& appearance_prefs() const noexcept {
    return appearance_;
  }
  [[nodiscard]] Result<void> SetUiText(std::string_view key,
                                       std::string_view text);
  void SetTextDirection(bool right_to_left);
  void SetNativeTitleBar(const NativeTitleBarMetrics& metrics);

  bool PutImage(ImageId id,
                int width,
                int height,
                int stride,
                const std::uint8_t* pixels);
  void DropImage(ImageId id);

  // UI-local state.
  void OpenCommandBar(std::string text, bool select_all);
  void OpenWorkspaceEditor(Space value, bool can_remove, TabId icon_tab = {});
  void CloseWorkspaceEditor();
  void CloseCommandBar();
  // Returns the request id the caller should send to the host.
  std::uint32_t SetCommandText(std::string text);
  void SetCommandSelectAll(bool value) { ui_.command_bar.select_all = value; }
  void SetCommandPointerOrigin();
  void MarkCommandTyped();
  void SetCommandEdit(std::string text,
                      AddressSelection selection,
                      bool native);
  // Ignores replies to anything but the latest request.
  void SetSuggestions(std::uint32_t request_id,
                      std::vector<Suggestion> suggestions,
                      std::uint64_t generation = 0);
  void MoveCommandSelection(int delta);
  void SetCommandSelection(std::size_t index);
  void SetSidebarRevealed(bool revealed);
  void SetToolbarRevealed(bool revealed);
  void BeginTabPreview(TabId tab, std::uint32_t request_id);
  void EndTabPreview();
  [[nodiscard]] bool SetTabPreview(std::uint32_t request_id,
                                   TabId tab,
                                   const TabPreviewData& data);

  // Reads.
  [[nodiscard]] std::span<const Tab> tabs() const { return tabs_; }
  [[nodiscard]] const Tab* FindTab(TabId id) const;
  [[nodiscard]] std::optional<std::size_t> IndexOf(TabId id) const;
  [[nodiscard]] TabId active_tab() const { return active_tab_; }
  [[nodiscard]] std::span<const Space> spaces() const { return spaces_; }
  [[nodiscard]] SpaceId active_space() const { return active_space_; }
  [[nodiscard]] const Space* FindSpace(SpaceId id) const;
  [[nodiscard]] const NavState& nav() const { return nav_; }
  [[nodiscard]] const LoadingProgressState& loading_progress() const noexcept {
    return loading_.state();
  }
  [[nodiscard]] std::span<const Action> actions() const { return actions_; }
  [[nodiscard]] const Theme& theme() const { return theme_; }
  [[nodiscard]] const WindowState& window() const { return window_; }
  [[nodiscard]] const LayoutPrefs& layout_prefs() const { return prefs_; }
  [[nodiscard]] const UiState& ui() const { return ui_; }
  // Accumulating input has no visual effect and must not schedule a render.
  [[nodiscard]] WorkspaceSwipeState& EditWorkspaceSwipe() noexcept {
    return ui_.workspace_swipe;
  }
  [[nodiscard]] UiTextView text() const noexcept {
    return {&text_, right_to_left_};
  }
  [[nodiscard]] const ImageStore& images() const { return images_; }
  ImageStore& images() { return images_; }

  [[nodiscard]] Flags<Dirty> dirty() const { return dirty_; }
  [[nodiscard]] const BrowserToolsState& browser_tools() const noexcept {
    return browser_tools_;
  }
  [[nodiscard]] BrowserToolsState& EditBrowserTools() noexcept {
    MarkDirty(Dirty::kBrowserTools);
    return browser_tools_;
  }
  // Returns what changed and resets the record.
  [[nodiscard]] const PrivacyFeaturesState& privacy_features() const noexcept {
    return privacy_features_;
  }
  [[nodiscard]] const LibraryState& library() const noexcept {
    return library_;
  }
  [[nodiscard]] LibraryState& EditLibrary() noexcept {
    MarkDirty(Dirty::kLibrary);
    return library_;
  }
  [[nodiscard]] PrivacyFeaturesState& EditPrivacyFeatures() noexcept {
    MarkDirty(Dirty::kPrivacyFeatures);
    return privacy_features_;
  }
  [[nodiscard]] const OnboardingState& onboarding() const noexcept {
    return onboarding_;
  }
  [[nodiscard]] OnboardingState& EditOnboarding() noexcept {
    MarkDirty(Dirty::kOnboarding);
    return onboarding_;
  }
  [[nodiscard]] const BrowserServicesState& browser_services() const noexcept {
    return services_;
  }
  [[nodiscard]] BrowserServicesState& EditBrowserServices() noexcept {
    MarkDirty(Dirty::kSettings | Dirty::kOnboarding);
    return services_;
  }
  [[nodiscard]] const BrowserSettingsState& browser_settings() const noexcept {
    return browser_settings_;
  }
  [[nodiscard]] BrowserSettingsState& EditBrowserSettings() noexcept {
    MarkDirty(Dirty::kSettings);
    return browser_settings_;
  }

  // The onboarding colour draft repaints the whole window while it is edited.
  void SetOnboardingThemeEditing(bool editing);
  [[nodiscard]] bool EditOnboardingTheme(const ThemeEdit& edit);
  // Copies a measured chrome element; marks dirty only when it moved.
  void SetOnboardingLanding(const Anchor& anchor);
  Flags<Dirty> TakeDirty();
  // Presents the unchanged tab list again, e.g. after a native drag that the
  // host did not apply.
  void InvalidateTabs() { MarkDirty(Dirty::kTabs); }

 private:
  void ForgetMultiPageTab(TabId id);
  void MarkDirty(Flags<Dirty> what) { dirty_ |= what; }
  void ReindexFrom(std::size_t first);

  std::vector<Tab> tabs_;
  std::vector<Folder> folders_;
  std::unordered_map<FolderId, std::size_t> folder_index_;
  std::unique_ptr<MultiPageState> multi_page_;
  AppearancePrefs appearance_;
  std::unordered_map<TabId, std::size_t> index_;
  TabId active_tab_;
  std::vector<Space> spaces_;
  SpaceId active_space_;
  NavState nav_;
  LoadingProgress loading_;
  std::vector<Action> actions_;
  Theme theme_;
  std::unordered_map<SpaceId, WorkspaceTheme> workspace_themes_;
  WindowState window_;
  LayoutPrefs prefs_;
  UiState ui_;
  BrowserToolsState browser_tools_;
  PrivacyFeaturesState privacy_features_;
  LibraryState library_;
  OnboardingState onboarding_;
  BrowserSettingsState browser_settings_;
  BrowserServicesState services_;
  std::array<std::string, kUiTextDefaults.size()> text_;
  bool right_to_left_ = false;
  ImageStore images_;
  std::uint32_t next_request_id_ = 1;
  Flags<Dirty> dirty_ = Dirty::kAll;
};

}  // namespace kr
