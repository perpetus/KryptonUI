#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/core/ids.hpp"
#include "krypton/ui/model/browser_tools.hpp"
#include "krypton/ui/model/model.hpp"
#include "krypton/ui/model/tab_drop.hpp"
#include "krypton/ui/model/workspace_swipe.hpp"
#include "krypton/ui/platform/glass.hpp"
#include "krypton/ui/presentation/multi_page.hpp"
#include "krypton/ui/presentation/view_model.hpp"

namespace kr {

using Task = std::move_only_function<void()>;

// Semantic user input, reported by a platform view. The Shell turns these
// into host intents or UI-local state changes; views never talk to the host.
class ShellViewDelegate {
 public:
  [[nodiscard]] virtual std::optional<SplitRowVm> OnSplitRowRequested(
      TabId tab) const {
    (void)tab;
    return std::nullopt;
  }
  [[nodiscard]] virtual std::optional<TabItemVm> OnSplitCandidateRequested(
      TabId tab) const {
    return OnTabRequested(tab);
  }
  virtual void OnTaskManagerDismissed() = 0;
  virtual void OnTaskSearchChanged(std::string_view text) = 0;
  virtual void OnTaskFilterChanged(TaskFilter filter) = 0;
  virtual void OnTaskSortChanged(TaskColumn column) = 0;
  virtual void OnTasksSelected(std::span<const TaskId> ids) = 0;
  virtual void OnEndTasks() = 0;
  virtual void OnTaskActivated(TaskId id) = 0;
  virtual void OnSitePermissionsDismissed() = 0;
  virtual void OnSitePermissionsRequested() = 0;
  virtual void OnPermissionSearchChanged(std::string_view text) = 0;
  virtual void OnSitePermissionChanged(PermissionId id,
                                       PermissionSetting setting) = 0;
  virtual void OnSitePermissionsReset() = 0;
  virtual void OnPermissionPromptDecision(std::uint64_t request,
                                          PermissionDecision decision) = 0;
  virtual void OnPrivacyFeatureChanged(PrivacyFeatureId id, bool enabled) = 0;
  virtual void OnPrivacyFeatureConfirmed(PrivacyFeatureId id,
                                         bool accepted) = 0;
  virtual void OnPrivacyFeatureManage(PrivacyFeatureId id) = 0;
  virtual void OnPrivacyRelaunch() = 0;
  // Downloads, sidebar media cards and the library panel.
  virtual void OnDownloadStackOpened() {}
  virtual void OnDownloadCommand(DownloadId id, DownloadCommand command) = 0;
  virtual void OnMediaCommand(MediaId id, MediaCommand command) = 0;
  virtual void OnMediaSeek(MediaId id, double seconds) = 0;
  virtual void OnLibraryToggled() = 0;
  virtual void OnLibrarySectionSelected(LibrarySection section) = 0;
  virtual void OnLibraryDismissed() = 0;
  virtual void OnLibrarySearchChanged(std::string_view query) = 0;
  [[nodiscard]] virtual bool OnRoutingSubmitted(const RoutingDraft& value) = 0;
  virtual void OnRoutingCancelled() = 0;
  virtual void OnLibraryCommand(LibrarySection section,
                                LibraryItemId item,
                                LibraryCommand command) = 0;
  virtual void OnBrowserSettingEdited(BrowserSetting setting,
                                      std::uint32_t value) = 0;
  virtual void OnSettingsAction(SettingsAction action) = 0;
  virtual void OnSettingsSearchEngine(SearchEngineId engine) = 0;
  [[nodiscard]] virtual Result<void> OnSettingsWorkspaceSubmitted(
      WorkspaceEditKind kind,
      SpaceId id,
      std::string_view name,
      std::string_view glyph) = 0;
  virtual void OnSettingsDismissed() = 0;
  virtual void OnSettingsDeactivated() = 0;
  [[nodiscard]] virtual Result<void> OnWorkspaceSubmitted(
      WorkspaceEditKind kind,
      std::string_view name,
      std::string_view glyph) = 0;
  virtual void OnWorkspaceEditorDismissed() = 0;
  virtual void OnSettingsSectionSelected(SettingsSection section) = 0;
  virtual void OnSettingsNavigationToggled() {}
  virtual void OnAppearanceEdited(AppearanceEdit edit) = 0;
  [[nodiscard]] virtual bool CanRequestGlassBackdrop() const { return false; }
  virtual void OnGlassBackdropRequested(const GlassBackdropRequest&) {}
  virtual void OnSettingsLayoutSelected(LayoutChoice choice) = 0;
  virtual void OnSettingsLayoutEdited(LayoutEdit edit) = 0;
  virtual void OnTabActivated(TabId tab) = 0;
  [[nodiscard]] virtual Result<void> OnFolderCommand(const FolderCommand&) {
    return Fail(ErrorCode::kUnsupported, "Folders are unavailable");
  }
  [[nodiscard]] virtual std::optional<FolderVm> OnFolderRequested(
      FolderId) const {
    return std::nullopt;
  }
  virtual void OnSplitPaneActivated(TabId tab) { OnTabActivated(tab); }
  [[nodiscard]] virtual bool OnSplitSelectionChanged(
      std::span<const TabId> tabs) {
    (void)tabs;
    return false;
  }
  virtual void OnSplitLayoutSelected(SplitLayout layout) { (void)layout; }
  [[nodiscard]] virtual bool OnBoostEdited(BoostEdit, const BoostDraft&) {
    return false;
  }
  virtual void OnSplitChooserDismissed() {}
  [[nodiscard]] virtual bool OnSplitChooserAccepted() { return false; }
  virtual void OnTabClosed(TabId tab) = 0;
  // The sidebar's Clear button: close the active space's idle unpinned tabs.
  virtual void OnClearTabs() = 0;
  virtual void OnTabMuteToggled(TabId tab) = 0;
  // A tab was dragged to sit directly before `next`, or directly after
  // `previous` at the end of its list, in `section`. The neighbours belong to
  // that section. The native list keeps its order until the host answers.
  virtual void OnTabDropped(TabId tab,
                            TabId previous,
                            TabId next,
                            TabDropSection section) = 0;
  virtual void OnThemeEdited(const ThemeEdit& edit) = 0;
  virtual void OnThemeEditorDismissed() = 0;
  [[nodiscard]] virtual std::uint32_t OnTabPreviewRequested(TabId tab) = 0;
  virtual void OnTabPreviewDismissed(std::uint32_t request_id) = 0;
  virtual void OnNewTab() = 0;
  virtual void OnUrlDropped(std::string_view url) = 0;
  virtual void OnSpaceSelected(SpaceId space) = 0;
  [[nodiscard]] virtual bool OnWorkspaceSwipe(WorkspaceSwipeInput input) = 0;
  virtual void OnNavCommand(NavCommand command) = 0;
  virtual void OnWindowCommand(WindowCommand command) = 0;
  virtual void OnActionInvoked(ActionId action, Rect anchor) = 0;
  virtual void OnMenuRequested(MenuKind kind,
                               std::uint64_t target,
                               Rect anchor) = 0;
  virtual void OnMenuCommand(std::uint32_t request_id,
                             std::int32_t command_id) = 0;
  virtual void OnMenuClosed(std::uint32_t request_id) = 0;

  // Command bar (address field + suggestions).
  virtual void OnCommandBarRequested() = 0;
  virtual void OnAddressPointerRequested() { OnCommandBarRequested(); }
  virtual void OnAddressFocusChanged(bool focused) = 0;
  virtual void OnCommandTextChanged(std::string_view text) = 0;
  virtual void OnAddressEditChanged(std::string_view text,
                                    AddressSelection selection) = 0;
  virtual void OnCommandSelectionMoved(int delta) = 0;
  virtual bool OnAddressKey(AddressKey key,
                            Flags<AddressModifier> modifiers) = 0;
  virtual void OnCommandAccepted(Disposition disposition) = 0;
  virtual void OnSuggestionChosen(std::size_t index,
                                  Disposition disposition) = 0;
  virtual void OnCommandBarDismissed() = 0;

  // Onboarding. Choices go to the Shell; only the Shell talks to the host.
  virtual void OnOnboardingAction(OnboardingAction action) = 0;
  // Hovering or focusing a layout previews it over the real window.
  virtual void OnOnboardingLayoutHovered(
      std::optional<LayoutChoice> choice) = 0;
  virtual void OnOnboardingLayoutChosen(LayoutChoice choice) = 0;
  virtual void OnOnboardingSideChosen(bool right) = 0;
  // The ghost finished morphing over the veiled chrome.
  virtual void OnOnboardingMorphSettled(std::uint32_t serial) = 0;
  // A press or drag on the colour field, in window client DIPs.
  virtual void OnOnboardingColorPoint(Point client) = 0;
  // `origin` is where the change was made, for the reveal.
  virtual void OnOnboardingThemeEdited(const ThemeEdit& edit, Point origin) = 0;
  // An empty id means "start fresh".
  virtual void OnOnboardingImportChosen(ImportSourceId source) = 0;
  virtual void OnOnboardingImportItemToggled(ImportData item, bool enabled) = 0;
  virtual void OnOnboardingSearchChosen(SearchEngineId engine) = 0;
  virtual void OnOnboardingDefaultChoice(bool make_default) = 0;
  // The finishing motion completed; the surface can be released.
  virtual void OnOnboardingHandoffFinished() = 0;

  virtual void OnSidebarRevealChanged(bool revealed) = 0;
  virtual void OnToolbarRevealChanged(bool revealed) = 0;
  virtual void OnNativeTitleBarChanged(
      const NativeTitleBarMetrics& metrics) = 0;
  // One of the sidebar's toggle buttons was pressed.
  virtual void OnSidebarToggled(SidebarToggle toggle) = 0;
  // The sidebar splitter was dragged to an expanded width, in DIPs.
  virtual void OnSidebarResized(float width) = 0;
  // The workspace indicator was clicked: move on to the next space.
  virtual void OnNextSpaceRequested() = 0;
  // Where native elements are, in window client DIPs, after a layout pass.
  virtual void OnAnchorsChanged(std::span<const Anchor> anchors) = 0;
  virtual void OnFocusPageRequested() = 0;
  // Called by the platform only for a key the module did not consume while
  // one of its input surfaces owns focus. The native message is borrowed.
  [[nodiscard]] virtual bool OnUnhandledKey(const void* native_message) = 0;
  // Demand lookup for a newly realized native row. Borrow only during the
  // synchronous copy; never retain the pointer across a host update.
  [[nodiscard]] virtual const Image* OnImageRequested(ImageId image) const = 0;
  // Current presentation for a recycled row, including realization between
  // renders. Copy borrowed strings synchronously before the next host call.
  [[nodiscard]] virtual std::optional<TabItemVm> OnTabRequested(
      TabId tab) const = 0;

 protected:
  ~ShellViewDelegate() = default;
};

// The native UI for one browser window. Implemented once per platform.
class ShellView {
 public:
  virtual ~ShellView() = default;

  // Apply a view model. Must reconcile, not rebuild: keep native elements
  // keyed by id and update only what changed. The model is valid only for
  // the duration of the call.
  virtual void Render(const ShellViewModel& vm) = 0;
  [[nodiscard]] virtual bool UrlDragChanged(Point point, bool active) = 0;
  [[nodiscard]] virtual Result<void> RegisterGlassBackdrop(
      const GlassBackdropResource&) = 0;
  [[nodiscard]] virtual Result<void> SubmitGlassBackdrop(
      const GlassBackdropFrame&) = 0;
  [[nodiscard]] virtual Result<void> DropGlassBackdrop(std::uint64_t id) = 0;

  // Show a menu the host built in answer to OnMenuRequested.
  virtual void ShowMenu(std::uint32_t request_id,
                        const MenuTree& menu,
                        Rect anchor,
                        const ImageStore& images,
                        MenuKind kind) = 0;
  virtual void DismissMenu(std::uint32_t request_id) = 0;
  virtual void FocusNative(Anchor target) = 0;
  virtual void ShowNotification(std::string_view text) = 0;

  // Run `task` on the UI thread at the next turn of the event loop.
  virtual void PostTask(Task task) = 0;
  // One-shot UI work. The queue remains idle when no tasks are pending.
  virtual void PostDelayedTask(Task task, std::chrono::milliseconds delay) = 0;

  // Legacy host-forwarded input. Windows owns its thread hook and leaves this
  // as a no-op. Other platform implementations may still use the ABI slot.
  virtual bool PreTranslateMessage(const void* /*native_message*/) {
    return false;
  }
  [[nodiscard]] virtual bool OwnsWindowFrame() const noexcept { return false; }
};

struct ShellViewParams {
  void* parent_window = nullptr;  // HWND on Windows, NSView* on macOS
  ThemeVm theme;
};

}  // namespace kr
