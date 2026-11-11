#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "krypton/ui/core/result.hpp"
#include "krypton/ui/layout/shell_layout.hpp"
#include "krypton/ui/model/model.hpp"
#include "krypton/ui/platform/shell_view.hpp"
#include "krypton/ui/presentation/presenter.hpp"
#include "krypton/ui/shell/browser_port.hpp"
#include "krypton/ui/shell/render_stats.hpp"
#include "krypton/ui/state/store.hpp"

namespace kr {

// One browser window's UI. Receives state from the host (through the ABI
// bridge), keeps it in the Store, renders at most once per UI-loop turn, and
// turns user input from the platform view into host intents.
class Shell final : public ShellViewDelegate {
  struct PassKey {
    explicit PassKey() = default;
  };

 public:
  [[nodiscard]] static Result<std::unique_ptr<Shell>> Create(
      void* parent_window,
      const KrBrowser& browser,
      const Theme& initial_theme = {});
  // Use Create(); the key keeps construction private while still allowing
  // std::make_unique.
  Shell(PassKey, const KrBrowser& browser);
  ~Shell();

  Shell(const Shell&) = delete;
  Shell& operator=(const Shell&) = delete;

  // Calls coalesce native rendering; window updates also report the host's
  // content rectangle synchronously, before returning.
  void SetWindowState(const WindowState& state);
  [[nodiscard]] Result<void> UpsertSplitGroup(const SplitGroup& group);
  [[nodiscard]] Result<void> RemoveSplitGroup(SplitGroupId id,
                                              std::uint64_t revision);
  [[nodiscard]] Result<void> SetGlance(const GlanceState& state);
  [[nodiscard]] std::optional<SplitRowVm> OnSplitRowRequested(
      TabId tab) const override;
  [[nodiscard]] std::optional<TabItemVm> OnSplitCandidateRequested(
      TabId tab) const override;
  void OnSplitPaneActivated(TabId tab) override;
  [[nodiscard]] Result<void> ShowSplitChooser();
  [[nodiscard]] Result<void> ShowSplitChooser(SplitLayout layout);
  [[nodiscard]] Result<void> ShowSplitOptions(TabId tab, Rect anchor);
  [[nodiscard]] bool OnSplitSelectionChanged(
      std::span<const TabId> tabs) override;
  void OnSplitLayoutSelected(SplitLayout layout) override;
  void OnSplitChooserDismissed() override;
  [[nodiscard]] bool OnSplitChooserAccepted() override;
  [[nodiscard]] Result<void> ShowTaskManager();
  [[nodiscard]] Result<void> UpsertTask(BrowserTask value);
  [[nodiscard]] Result<void> RemoveTask(TaskId id);
  [[nodiscard]] Result<void> ShowSitePermissions(std::string origin);
  [[nodiscard]] Result<void> UpsertSitePermission(SitePermission value);
  [[nodiscard]] Result<void> RemoveSitePermission(PermissionId id);
  [[nodiscard]] Result<void> ShowPermissionPrompt(PermissionPrompt value);
  void DismissPermissionPrompt(std::uint64_t request);
  [[nodiscard]] bool UrlDragChanged(Point point, bool active);
  void OnTaskManagerDismissed() override;
  void OnTaskSearchChanged(std::string_view text) override;
  void OnTaskFilterChanged(TaskFilter filter) override;
  void OnTaskSortChanged(TaskColumn column) override;
  void OnTasksSelected(std::span<const TaskId> ids) override;
  void OnEndTasks() override;
  void OnTaskActivated(TaskId id) override;
  void OnSitePermissionsDismissed() override;
  void OnSitePermissionsRequested() override;
  void OnPermissionSearchChanged(std::string_view text) override;
  void OnSitePermissionChanged(PermissionId id,
                               PermissionSetting setting) override;
  void OnSitePermissionsReset() override;
  void OnPermissionPromptDecision(std::uint64_t request,
                                  PermissionDecision decision) override;
  void SetTheme(const Theme& theme);
  [[nodiscard]] Result<void> SetWorkspaceTheme(SpaceId space,
                                               const WorkspaceTheme& theme);
  [[nodiscard]] Result<void> ShowThemeEditor(SpaceId space);
  void OnThemeEdited(const ThemeEdit& edit) override;
  void OnThemeEditorDismissed() override;
  void SetLayoutPrefs(const LayoutPrefs& prefs);
  [[nodiscard]] Result<void> SetAppearancePrefs(
      const AppearancePrefs& prefs,
      bool icon_pack_supplied = false);
  [[nodiscard]] Result<void> ShowSettings(
      std::optional<SettingsSection> section);
  [[nodiscard]] Result<void> UpsertPrivacyFeature(PrivacyFeature value);
  [[nodiscard]] Result<void> RemovePrivacyFeature(PrivacyFeatureId id);
  void OnPrivacyFeatureChanged(PrivacyFeatureId id, bool enabled) override;
  void OnPrivacyFeatureConfirmed(PrivacyFeatureId id, bool accepted) override;
  void OnPrivacyFeatureManage(PrivacyFeatureId id) override;
  void OnPrivacyRelaunch() override;
  [[nodiscard]] Result<void> UpsertDownload(Download value);
  [[nodiscard]] Result<void> RemoveDownload(DownloadId id);
  [[nodiscard]] Result<void> UpsertMediaSession(MediaSession value);
  [[nodiscard]] Result<void> RemoveMediaSession(MediaId id);
  [[nodiscard]] Result<void> SetLibrarySection(LibrarySection section,
                                               Flags<LibrarySectionFlag> flags);
  [[nodiscard]] Result<void> UpsertLibraryItem(LibraryItem value,
                                               LibraryItemId before);
  [[nodiscard]] Result<void> RemoveLibraryItem(LibraryItemId id);
  [[nodiscard]] Result<void> ShowLibrary(LibrarySection section);
  [[nodiscard]] Result<void> ShowOnboarding(bool first_run,
                                            OnboardingStep step);
  [[nodiscard]] Result<void> UpsertImportSource(ImportSource value);
  [[nodiscard]] Result<void> RemoveImportSource(ImportSourceId id);
  [[nodiscard]] Result<void> SetImportProgress(const ImportProgress& value);
  [[nodiscard]] Result<void> SetDefaultBrowserState(
      Flags<DefaultBrowserFlag> flags);
  [[nodiscard]] Result<void> UpsertSearchEngine(SearchEngine value);
  [[nodiscard]] Result<void> RemoveSearchEngine(SearchEngineId id);
  void OnOnboardingAction(OnboardingAction action) override;
  void OnOnboardingLayoutHovered(std::optional<LayoutChoice> choice) override;
  void OnOnboardingLayoutChosen(LayoutChoice choice) override;
  void OnOnboardingSideChosen(bool right) override;
  void OnOnboardingMorphSettled(std::uint32_t serial) override;
  void OnOnboardingColorPoint(Point client) override;
  void OnOnboardingThemeEdited(const ThemeEdit& edit, Point origin) override;
  void OnOnboardingImportChosen(ImportSourceId source) override;
  void OnOnboardingImportItemToggled(ImportData item, bool enabled) override;
  void OnOnboardingSearchChosen(SearchEngineId engine) override;
  void OnOnboardingDefaultChoice(bool make_default) override;
  void OnOnboardingHandoffFinished() override;
  void OnDownloadStackOpened() override;
  void OnDownloadCommand(DownloadId id, DownloadCommand command) override;
  void OnMediaCommand(MediaId id, MediaCommand command) override;
  void OnMediaSeek(MediaId id, double seconds) override;
  void OnLibraryToggled() override;
  void OnLibrarySectionSelected(LibrarySection section) override;
  void OnLibraryDismissed() override;
  void OnLibrarySearchChanged(std::string_view query) override;
  [[nodiscard]] bool OnRoutingSubmitted(const RoutingDraft& value) override;
  void OnRoutingCancelled() override;
  [[nodiscard]] Result<void> ShowRoutingEditor(RoutingDraft value);
  void OnLibraryCommand(LibrarySection section,
                        LibraryItemId item,
                        LibraryCommand command) override;
  [[nodiscard]] Result<void> SetBrowserSettings(
      BrowserSettingsSnapshot snapshot);
  [[nodiscard]] Result<void> CompleteSettingsOperation(std::uint64_t request,
                                                       bool success);
  void OnBrowserSettingEdited(BrowserSetting setting,
                              std::uint32_t value) override;
  void OnSettingsAction(SettingsAction action) override;
  void OnSettingsSearchEngine(SearchEngineId engine) override;
  [[nodiscard]] Result<void> OnSettingsWorkspaceSubmitted(
      WorkspaceEditKind kind,
      SpaceId id,
      std::string_view name,
      std::string_view glyph) override;
  void OnSettingsDismissed() override;
  void OnSettingsDeactivated() override;
  void OnSettingsSectionSelected(SettingsSection section) override;
  void OnSettingsNavigationToggled() override;
  void OnAppearanceEdited(AppearanceEdit edit) override;
  [[nodiscard]] bool CanRequestGlassBackdrop() const override;
  void OnGlassBackdropRequested(const GlassBackdropRequest& request) override;
  [[nodiscard]] Result<void> RegisterGlassBackdrop(
      const GlassBackdropResource& resource) {
    return view_->RegisterGlassBackdrop(resource);
  }
  [[nodiscard]] Result<void> SubmitGlassBackdrop(
      const GlassBackdropFrame& frame) {
    return view_->SubmitGlassBackdrop(frame);
  }
  [[nodiscard]] Result<void> DropGlassBackdrop(std::uint64_t id) {
    return view_->DropGlassBackdrop(id);
  }
  void OnSettingsLayoutSelected(LayoutChoice choice) override;
  void OnSettingsLayoutEdited(LayoutEdit edit) override;
  [[nodiscard]] Result<void> SetUiText(std::string_view key,
                                       std::string_view text);
  void SetTextDirection(bool right_to_left);
  void PutImage(ImageId id,
                int width,
                int height,
                int stride,
                const std::uint8_t* pixels);
  void DropImage(ImageId id);
  void SetSpaces(std::vector<Space> spaces, SpaceId active);
  void InsertTab(std::size_t index, Tab tab);
  [[nodiscard]] Result<void> UpsertFolder(Folder folder);
  [[nodiscard]] Result<void> RemoveFolder(FolderId id);
  [[nodiscard]] Result<void> OnFolderCommand(
      const FolderCommand& command) override;
  [[nodiscard]] std::optional<FolderVm> OnFolderRequested(
      FolderId id) const override;
  void UpdateTab(Tab tab);
  void MoveTab(TabId id, std::size_t to_index);
  void RemoveTab(TabId id);
  void SetActiveTab(TabId id);
  void SetNav(NavState nav);
  [[nodiscard]] Result<void> SetLoadProgress(const LoadProgressUpdate& update);
  void SetActions(std::vector<Action> actions);
  void SetSuggestions(std::uint32_t request_id,
                      std::vector<Suggestion> suggestions);
  void ShowMenu(std::uint32_t request_id, const MenuTree& menu);
  void FocusAddress(bool select_all);
  void SetAddressText(std::string text, bool select_all);
  [[nodiscard]] Result<void> SetAddressEdit(std::uint32_t request,
                                            std::uint64_t revision,
                                            std::string text,
                                            AddressSelection selection);
  void DismissAddress();
  [[nodiscard]] Result<void> FocusNative(Anchor target);
  [[nodiscard]] Result<void> ShowNotification(std::string_view text);
  [[nodiscard]] Result<void> SetSuggestionSnapshot(
      std::uint32_t request_id,
      std::uint64_t generation,
      std::vector<Suggestion> suggestions);
  void DismissMenu(std::uint32_t request_id);
  [[nodiscard]] Result<void> ShowWorkspaceEditor(SpaceId space);
  void DismissBoostEditor();
  [[nodiscard]] Result<void> ShowBoostEditor(BoostDraft value);
  [[nodiscard]] bool OnBoostEdited(BoostEdit kind,
                                   const BoostDraft& value) override;
  [[nodiscard]] Result<void> ShowTabIconEditor(TabId tab,
                                               std::string_view glyph);
  [[nodiscard]] Result<void> OnWorkspaceSubmitted(
      WorkspaceEditKind kind,
      std::string_view name,
      std::string_view glyph) override;
  void OnWorkspaceEditorDismissed() override;
  [[nodiscard]] bool PreTranslateMessage(const void* native_message);

  [[nodiscard]] const Store& store() const { return store_; }
  [[nodiscard]] std::uint64_t render_count() const { return stats_.count(); }

  // ShellViewDelegate:
  void OnTabActivated(TabId tab) override;
  void OnTabClosed(TabId tab) override;
  void OnClearTabs() override;
  void OnTabMuteToggled(TabId tab) override;
  void OnTabDropped(TabId tab,
                    TabId previous,
                    TabId next,
                    TabDropSection section) override;
  [[nodiscard]] std::uint32_t OnTabPreviewRequested(TabId tab) override;
  void OnTabPreviewDismissed(std::uint32_t request_id) override;
  void SetTabPreview(std::uint32_t request_id,
                     TabId tab,
                     const TabPreviewData& data);
  void OnNewTab() override;
  void OnUrlDropped(std::string_view url) override;
  void OnSpaceSelected(SpaceId space) override;
  [[nodiscard]] bool OnWorkspaceSwipe(WorkspaceSwipeInput input) override;
  void OnNavCommand(NavCommand command) override;
  void OnWindowCommand(WindowCommand command) override;
  void OnActionInvoked(ActionId action, Rect anchor) override;
  void OnMenuRequested(MenuKind kind,
                       std::uint64_t target,
                       Rect anchor) override;
  void OnMenuCommand(std::uint32_t request_id,
                     std::int32_t command_id) override;
  void OnMenuClosed(std::uint32_t request_id) override;
  void OnCommandBarRequested() override;
  void OnAddressPointerRequested() override;
  void OnAddressFocusChanged(bool focused) override;
  void OnCommandTextChanged(std::string_view text) override;
  void OnAddressEditChanged(std::string_view text,
                            AddressSelection selection) override;
  void OnCommandSelectionMoved(int delta) override;
  bool OnAddressKey(AddressKey key, Flags<AddressModifier> modifiers) override;
  [[nodiscard]] Result<void> SetSuggestionSelection(std::uint32_t request,
                                                    std::uint64_t generation,
                                                    std::size_t index);
  void OnCommandAccepted(Disposition disposition) override;
  void OnSuggestionChosen(std::size_t index, Disposition disposition) override;
  void OnCommandBarDismissed() override;
  void OnSidebarRevealChanged(bool revealed) override;
  void OnToolbarRevealChanged(bool revealed) override;
  void OnNativeTitleBarChanged(const NativeTitleBarMetrics& metrics) override;
  void OnSidebarToggled(SidebarToggle toggle) override;
  void OnSidebarResized(float width) override;
  void OnNextSpaceRequested() override;
  void OnAnchorsChanged(std::span<const Anchor> anchors) override;
  void OnFocusPageRequested() override;
  [[nodiscard]] bool OnUnhandledKey(const void* native_message) override;
  [[nodiscard]] const Image* OnImageRequested(ImageId image) const override;
  [[nodiscard]] std::optional<TabItemVm> OnTabRequested(
      TabId tab) const override;

 private:
  static void LogToHost(void* self, LogLevel level, std::string_view text);
  // Records a host-side mistake (unknown tab id...) without failing.
  void Check(const Result<void>& result, std::string_view what);
  void ScheduleRender();
  void RenderNow();
  [[nodiscard]] ShellLayout CurrentLayout() const;
  void ReportContentBounds(const ShellLayout& layout);
  void Submit(std::optional<std::size_t> suggestion, Disposition disposition);
  void NotifyAddressState();
  void CancelAddressQuery();
  void CloseAddress();
  // Tells the host which library section is on screen, if any.
  void NotifyLibraryVisibility();
  // Onboarding owns the window while it shows; other surfaces wait.
  [[nodiscard]] bool OnboardingActive() const noexcept {
    return store_.onboarding().open;
  }
  void GoToOnboardingStep(OnboardingStep step, int direction);
  void CommitOnboardingStep();
  void LeaveOnboardingColor(bool commit);
  void EnterOnboardingColor();
  void LandOnboarding(Element target, ImageId icon);
  void FinishOnboarding(OnboardingOutcome outcome);
  void CloseOnboarding();
  void RevealMediaLater(MediaId id);

  struct AnchorKey {
    AnchorKind kind;
    std::uint64_t id;
    friend bool operator==(const AnchorKey&, const AnchorKey&) = default;
  };
  struct AnchorKeyHash {
    std::size_t operator()(const AnchorKey& key) const noexcept;
  };
  struct CachedAnchor {
    Anchor value;
    bool seen = false;
  };

  BrowserPort browser_;
  std::uint32_t rendered_request_ = 0;
  std::uint64_t rendered_generation_ = 0;
  std::size_t rendered_suggestion_count_ = 0;
  int rendered_selection_ = 0;
  std::optional<std::pair<float, float>> last_metrics_;
  Store store_;
  Presenter presenter_;
  RenderStats stats_;
  std::unique_ptr<ShellView> view_;

  std::optional<ShellLayout> last_layout_;
  std::optional<std::pair<Rect, float>> last_content_bounds_;
  std::optional<std::pair<Color, Color>> last_background_;
  std::optional<Color> last_pane_outline_;
  std::array<Color, 5> last_glance_colors_{};
  bool last_transparent_chrome_ = false;
  std::optional<WorkspaceTheme> last_window_theme_;
  SpaceId last_window_theme_space_;
  bool last_window_theme_dark_ = false;
  // Keep nodes between layout passes; clearing an unordered_map would free
  // and reallocate one node per anchor on every render.
  std::unordered_map<AnchorKey, CachedAnchor, AnchorKeyHash> anchors_;
  struct MenuAnchor {
    Rect bounds;
    MenuKind kind;
  };
  std::unordered_map<std::uint32_t, MenuAnchor> menu_anchors_;
  std::uint32_t next_menu_request_ = 1;
  bool command_opens_new_tab_ = false;
  bool render_scheduled_ = false;
  std::uint32_t next_tab_preview_request_ = 0;
  std::uint64_t sidebar_hover_serial_ = 0;
  std::uint64_t toolbar_hover_serial_ = 0;
  // Posted tasks hold a weak_ptr to this, so they do nothing once the Shell
  // is gone.
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  std::uint64_t native_focus_serial_ = 0;
  std::optional<LibrarySection> library_visible_;
};

}  // namespace kr
