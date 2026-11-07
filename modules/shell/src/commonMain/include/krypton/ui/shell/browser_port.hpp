#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/core/ids.hpp"
#include "krypton/ui/core/log.hpp"
#include "krypton/ui/model/address.hpp"
#include "krypton/ui/model/boost.hpp"
#include "krypton/ui/model/browser_tools.hpp"
#include "krypton/ui/model/library.hpp"
#include "krypton/ui/model/model.hpp"
#include "krypton/ui/model/onboarding.hpp"
#include "krypton/ui/model/privacy_features.hpp"
#include "krypton/ui/model/settings.hpp"
#include "krypton/ui/model/workspace_editor.hpp"
#include "krypton/ui/platform/glass.hpp"
#include "krypton_ui/abi.h"

namespace kr {
struct ThemeVm;

// Typed, null-safe access to the host's KrBrowser table: the only way
// intents leave the module.
//
// The table is copied on construction. Only complete slots within the host's
// struct_size are callable; a missing or incomplete slot is a no-op.
class BrowserPort {
 public:
  [[nodiscard]] bool CanFolders() const noexcept;
  [[nodiscard]] Result<void> FolderCommand(
      const kr::FolderCommand& command) const;
  [[nodiscard]] bool CanMultiPage() const noexcept {
    return (multi_page_capabilities_ & KR_MULTI_PAGE_COMPLETE) ==
           KR_MULTI_PAGE_COMPLETE;
  }
  [[nodiscard]] bool CanSnapLayouts() const noexcept {
    return CanMultiPage() &&
           (multi_page_capabilities_ & KR_MULTI_PAGE_SNAP_LAYOUTS) != 0;
  }
  [[nodiscard]] Result<void> MultiPageCommand(
      const KrMultiPageCommand& command) const;
  void TaskManagerVisibilityChanged(bool visible) const;
  void ActivateTask(TaskId id) const;
  [[nodiscard]] bool CanActivateTasks() const noexcept;
  [[nodiscard]] bool CanEndTasks() const noexcept;
  [[nodiscard]] Result<void> EndTasks(std::span<const TaskId> tasks) const;
  void SitePermissionsVisibilityChanged(std::string_view origin,
                                        bool visible) const;
  [[nodiscard]] bool CanEditSitePermissions() const noexcept;
  [[nodiscard]] bool CanResetSitePermissions() const noexcept;
  [[nodiscard]] Result<void> SetSitePermission(PermissionId id,
                                               PermissionSetting setting) const;
  [[nodiscard]] Result<void> ResetSitePermissions(
      std::string_view origin) const;
  [[nodiscard]] bool CanRespondToPermissionPrompt() const noexcept;
  void RespondToPermissionPrompt(std::uint64_t request,
                                 PermissionDecision decision) const;
  [[nodiscard]] bool CanEditPrivacyFeatures() const noexcept;
  [[nodiscard]] bool CanRelaunch() const noexcept;
  void PrivacyFeaturesVisibilityChanged(bool visible) const;
  [[nodiscard]] Result<void> SetPrivacyFeature(PrivacyFeatureId id,
                                               bool enabled) const;
  void Relaunch() const;
  void SendDownloadCommand(DownloadId id, DownloadCommand command) const;
  void SendMediaCommand(MediaId id, MediaCommand command) const;
  void SeekMedia(MediaId id, double seconds) const;
  [[nodiscard]] bool CanSeekMedia() const noexcept;
  void LibraryVisibilityChanged(LibrarySection section, bool visible) const;
  void SendLibraryCommand(LibrarySection section,
                          LibraryItemId item,
                          LibraryCommand command) const;
  [[nodiscard]] bool CanOnboard() const noexcept;
  [[nodiscard]] bool CanImport() const noexcept;
  [[nodiscard]] bool CanSetDefaultBrowser() const noexcept;
  [[nodiscard]] bool CanChooseSearch() const noexcept;
  void OnboardingVisibilityChanged(bool visible) const;
  [[nodiscard]] Result<void> StartImport(ImportSourceId source,
                                         Flags<ImportData> items) const;
  [[nodiscard]] Result<void> RequestDefaultBrowser() const;
  [[nodiscard]] Result<void> SetDefaultSearchEngine(SearchEngineId id) const;
  void OnboardingFinished(OnboardingOutcome outcome,
                          OnboardingStep last_step) const;
  explicit BrowserPort(const KrBrowser& browser) noexcept;

  void ActivateTab(TabId tab) const;
  void CloseTab(TabId tab) const;
  void SetTabFlags(TabId tab, Flags<TabFlag> mask, Flags<TabFlag> values) const;
  void RequestTabPreview(std::uint32_t request_id, TabId tab) const;
  void CancelTabPreview(std::uint32_t request_id) const;
  [[nodiscard]] bool CanEditTheme() const noexcept;
  void SetWorkspaceTheme(SpaceId space,
                         const WorkspaceTheme& theme,
                         bool commit) const;
  void SetWindowTheme(SpaceId space,
                      const WorkspaceTheme& theme,
                      bool dark) const;
  void NewTab() const;
  void MoveTab(TabId tab, SpaceId space, std::size_t to_index) const;
  void SwitchSpace(SpaceId space) const;

  void Navigate(std::string_view text, Disposition disposition) const;
  void SendNavCommand(kr::NavCommand command) const;
  void QuerySuggestions(std::uint32_t request_id, std::string_view text) const;
  void CancelSuggestions(std::uint32_t request_id) const;
  void AddressStateChanged(Flags<AddressFlag> flags,
                           std::uint32_t request_id,
                           std::string_view text,
                           AddressSelection selection,
                           std::uint64_t revision) const;
  [[nodiscard]] bool SupportsSuggestionSnapshots() const noexcept;
  [[nodiscard]] bool HandleAddressKey(AddressKey key,
                                      Flags<AddressModifier> modifiers) const;
  void SelectSuggestionSnapshot(std::uint32_t request,
                                std::uint64_t generation,
                                std::size_t index) const;
  void AcceptSuggestionSnapshot(std::uint32_t request_id,
                                std::uint64_t generation,
                                std::size_t index,
                                Disposition disposition) const;
  [[nodiscard]] bool CanEditWorkspaces() const noexcept;
  [[nodiscard]] bool CanSetTabIcon() const noexcept;
  [[nodiscard]] bool EditBoost(BoostEdit kind, const BoostDraft& value) const;
  void RefreshDownloads() const;
  [[nodiscard]] bool SaveRoutingRule(const RoutingDraft& value) const;
  void SetTabIcon(TabId tab, std::string_view glyph) const;
  void EditWorkspace(WorkspaceEditKind kind, const Space& space) const;
  void SetShellMetrics(float width, float height) const;
  void SetWindowBackdrop(const ThemeVm& theme) const;
  void AcceptSuggestion(std::uint32_t request_id,
                        std::size_t index,
                        Disposition disposition) const;

  void InvokeAction(ActionId action, Rect anchor) const;
  void RequestMenu(std::uint32_t request_id,
                   MenuKind kind,
                   std::uint64_t target,
                   Rect anchor) const;
  void MenuCommand(std::uint32_t request_id, std::int32_t command_id) const;
  void MenuClosed(std::uint32_t request_id) const;

  void SendWindowCommand(kr::WindowCommand command) const;
  void SetContentBounds(Rect bounds, float corner_radius) const;
  void SetWindowBackground(Color start, Color end) const;
  void SetAnchor(const Anchor& anchor) const;
  void SetDragRegions(std::span<const Rect> regions) const;
  void SetLayoutPrefs(const LayoutPrefs& prefs) const;
  void SettingsVisibilityChanged(SettingsSection section, bool visible) const;
  [[nodiscard]] bool CanSwitchWorkspaces() const noexcept;
  [[nodiscard]] bool CanEditSettings() const noexcept;
  [[nodiscard]] bool CanInvokeSettings() const noexcept;
  [[nodiscard]] Result<void> EditBrowserSetting(
      const KrSettingsEdit& edit) const;
  [[nodiscard]] Result<void> InvokeSettingsAction(std::uint64_t request,
                                                  SettingsAction action) const;
  [[nodiscard]] bool CanEditAppearance() const noexcept;
  [[nodiscard]] bool CanRequestGlassBackdrop() const noexcept;
  void RequestGlassBackdrop(const GlassBackdropRequest& request) const;
  [[nodiscard]] bool CanEditLayout() const noexcept;
  void SetAppearancePrefs(const AppearancePrefs& prefs) const;
  void FocusPage() const;
  [[nodiscard]] bool UnhandledKey(const void* native_message) const;

  void Log(LogLevel level, std::string_view text) const;
  [[nodiscard]] bool can_log() const {
    return KR_HAS_SLOT(&table_, KrBrowser, log) && table_.log != nullptr;
  }

 private:
  KrBrowser table_{};
  std::uint32_t multi_page_capabilities_ = 0;
};

}  // namespace kr
