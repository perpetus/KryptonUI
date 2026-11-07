#pragma once

#include <array>
#include <string>
#include <vector>

#include "krypton/ui/layout/shell_layout.hpp"
#include "krypton/ui/presentation/view_model.hpp"
#include "krypton/ui/state/store.hpp"

namespace kr {

[[nodiscard]] TabItemVm PresentTabItem(const Tab& tab, TabId active);

// Turns Store state into a ShellViewModel. Pure apart from reusing its own
// buffers. Dirty domains refresh borrowed collections after Store changes;
// unrelated renders reuse them without allocating or rebuilding tab rows.
class Presenter {
 public:
  [[nodiscard]] const ShellViewModel& Present(const Store& store,
                                              const ShellLayout& layout,
                                              Flags<Dirty> changed,
                                              bool snap_layouts = false,
                                              bool folders = false);

 private:
  std::vector<SettingsEntry> settings_entries_;
  std::vector<SettingsSectionItem> settings_sections_;
  void PresentSettings(const Store& store);
  std::vector<PrivacyFeatureVm> privacy_features_;
  LibraryPresenter library_;
  void PresentPrivacyFeatures(const PrivacyFeaturesState& state);
  void PresentOnboarding(const Store& store, const ShellLayout& layout);
  std::array<OnboardingStep, kOnboardingStepCount> rail_{};
  std::vector<PrivacyFeatureVm> onboarding_privacy_;
  std::string progress_, import_status_;
  void PresentTabs(const Store& store);
  [[nodiscard]] bool PresentActiveTab(const Store& store);
  void CacheActiveGroupRows(const Store& store);
  void PresentActions(const Store& store);
  void PresentSpaces(const Store& store);
  void PresentCommandBar(const Store& store);
  void PresentTabPreview(const Store& store);
  void PresentBrowserTools(const BrowserToolsState& tools);
  [[nodiscard]] static ThemeVm PresentTheme(const Store& store);

  std::vector<TabItemVm> favorites_;
  std::vector<TabItemVm> horizontal_tabs_;
  std::vector<TabItemVm> pinned_;
  std::vector<FolderVm> folders_;
  std::vector<TabItemVm> folder_tabs_;
  void PresentFolders(const Store& store);
  struct FolderLinks {
    std::size_t child, sibling, tab, last_tab;
    bool active = false;
  };
  std::vector<FolderLinks> folder_links_;
  std::vector<std::size_t> folder_order_, folder_next_tab_;
  std::vector<FolderRowVm> folder_rows_;
  std::vector<TabItemVm> today_;
  std::vector<TabItemVm> split_candidates_;
  SplitGroupId active_group_;
  std::array<TabItemVm*, 2> active_group_rows_{};
  bool today_idle_ = false;
  std::vector<ActionVm> page_actions_;
  std::vector<ActionVm> toolbar_actions_;
  std::vector<SpaceVm> spaces_;
  std::vector<SuggestionVm> suggestions_;
  ShellViewModel vm_;
  const Store* source_ = nullptr;
  bool horizontal_ = false;
  std::string preview_memory_, preview_memory_text_, preview_status_;
  struct TaskText {
    std::string memory, cpu, network, process;
  };
  std::vector<TaskText> task_text_;
  std::vector<const BrowserTask*> task_order_;
  std::vector<BrowserTaskVm> task_rows_;
  std::vector<SitePermissionVm> permission_rows_;
  std::string task_status_, permission_status_;
  std::uint64_t task_revision_ = ~std::uint64_t{0},
                permission_revision_ = ~std::uint64_t{0};
};

}  // namespace kr
