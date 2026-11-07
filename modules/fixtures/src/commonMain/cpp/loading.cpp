#include <array>

#include "krypton/fixtures/mock_browser.hpp"
#include "krypton/ui/design/tokens.generated.hpp"

namespace kr::fixtures {
void MockBrowser::PushLoadProgress(const MockTab& tab) const {
  if (!attached_ || !tab.navigation_id ||
      !KR_HAS_SLOT(&ui_, KrShellUI, set_load_progress) ||
      !ui_.set_load_progress)
    return;
  const KrLoadProgress value{
      sizeof(KrLoadProgress), tab.id, tab.navigation_id, tab.load_progress,
      KR_LOAD_PROGRESS_ENABLED |
          ((tab.flags & KR_TAB_LOADING) ? KR_LOAD_PROGRESS_LOADING : 0u)};
  (void)ui_.set_load_progress(ui_.self, &value);
}

void MockBrowser::StartLoading(MockTab& tab) {
  if (!callbacks_.load_requested ||
      (!tab.url.starts_with("http://") && !tab.url.starts_with("https://"))) {
    if (tab.flags & KR_TAB_LOADING) {
      tab.flags &= ~KR_TAB_LOADING;
      tab.load_progress = 1;
      PushTab(tab);
      if (tab.id == active_tab_)
        PushNav();
    }
    return;
  }
  tab.navigation_id = next_navigation_id_++;
  tab.load_step = 0;
  tab.load_progress = design::kTokens.loading_progress.minimum_progress;
  tab.flags |= KR_TAB_LOADING;
  PushTab(tab);
  if (tab.id == active_tab_)
    PushNav();
  if (!callbacks_.load_requested(tab.id, tab.navigation_id)) {
    tab.flags &= ~KR_TAB_LOADING;
    tab.load_progress = 1;
    PushTab(tab);
    if (tab.id == active_tab_)
      PushNav();
  }
}

bool MockBrowser::AdvanceLoading(KrTabId id, std::uint64_t navigation) {
  auto* tab = Find(id);
  if (!tab || tab->navigation_id != navigation ||
      !(tab->flags & KR_TAB_LOADING))
    return false;
  // Deliberate fixture samples, not estimates used by the production shell.
  constexpr std::array steps{0.2f, 0.45f, 0.7f, 0.94f, 1.0f};
  if (tab->load_step >= steps.size())
    return false;
  tab->load_progress = steps[tab->load_step++];
  if (tab->load_progress == 1)
    tab->flags &= ~KR_TAB_LOADING;
  PushTab(*tab);
  if (id == active_tab_)
    PushNav();
  return (tab->flags & KR_TAB_LOADING) != 0;
}
}  // namespace kr::fixtures
