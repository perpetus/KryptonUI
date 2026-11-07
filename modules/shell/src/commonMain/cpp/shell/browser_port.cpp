#include "krypton/ui/shell/browser_port.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

#include "krypton/ui/shell/theme_contract.hpp"

namespace kr {

namespace {

KrStr ToKr(std::string_view text) {
  return {text.data(), text.size()};
}
KrRect ToKr(const Rect& r) {
  return {r.x, r.y, r.width, r.height};
}

}  // namespace

BrowserPort::BrowserPort(const KrBrowser& browser) noexcept {
  // Copy only what the host has; everything past it stays null.
  const std::size_t size = std::min(browser.struct_size, sizeof(KrBrowser));
  std::memcpy(&table_, &browser, size);
  table_.struct_size = size;
  if (KR_HAS_SLOT(&table_, KrBrowser, multi_page_command) &&
      table_.multi_page_command && table_.multi_page_capabilities)
    multi_page_capabilities_ = table_.multi_page_capabilities(table_.self);
}

void BrowserPort::ActivateTab(TabId tab) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, activate_tab) && table_.activate_tab)
    table_.activate_tab(table_.self, tab.value);
}

void BrowserPort::RequestTabPreview(std::uint32_t request_id, TabId tab) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, request_tab_preview) &&
      table_.request_tab_preview)
    table_.request_tab_preview(table_.self, request_id, tab.value);
}

void BrowserPort::CancelTabPreview(std::uint32_t request_id) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, cancel_tab_preview) &&
      table_.cancel_tab_preview)
    table_.cancel_tab_preview(table_.self, request_id);
}

void BrowserPort::CloseTab(TabId tab) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, close_tab) && table_.close_tab)
    table_.close_tab(table_.self, tab.value);
}

bool BrowserPort::CanEditTheme() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, set_workspace_theme) &&
         table_.set_workspace_theme;
}
void BrowserPort::SetWorkspaceTheme(SpaceId space,
                                    const WorkspaceTheme& theme,
                                    bool commit) const {
  if (CanEditTheme()) {
    const auto data = ToThemeContract(space, theme);
    table_.set_workspace_theme(table_.self, &data, commit);
  }
}
void BrowserPort::SetWindowTheme(SpaceId space,
                                 const WorkspaceTheme& theme,
                                 bool dark) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, set_window_theme) &&
      table_.set_window_theme) {
    const auto data = ToThemeContract(space, theme);
    table_.set_window_theme(table_.self, &data, dark);
  }
}

void BrowserPort::NewTab() const {
  if (KR_HAS_SLOT(&table_, KrBrowser, new_tab) && table_.new_tab)
    table_.new_tab(table_.self);
}

void BrowserPort::SetTabFlags(TabId tab,
                              Flags<TabFlag> mask,
                              Flags<TabFlag> values) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, set_tab_flags) && table_.set_tab_flags)
    table_.set_tab_flags(table_.self, tab.value, mask.bits(), values.bits());
}

void BrowserPort::MoveTab(TabId tab,
                          SpaceId space,
                          std::size_t to_index) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, move_tab) && table_.move_tab)
    table_.move_tab(table_.self, tab.value, space.value, to_index);
}

void BrowserPort::SwitchSpace(SpaceId space) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, switch_space) && table_.switch_space)
    table_.switch_space(table_.self, space.value);
}

void BrowserPort::Navigate(std::string_view text,
                           Disposition disposition) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, navigate) && table_.navigate) {
    table_.navigate(table_.self, ToKr(text), std::to_underlying(disposition));
  }
}

void BrowserPort::SendNavCommand(kr::NavCommand command) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, nav_command) && table_.nav_command) {
    table_.nav_command(table_.self, std::to_underlying(command));
  }
}

void BrowserPort::QuerySuggestions(std::uint32_t request_id,
                                   std::string_view text) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, query_suggestions) &&
      table_.query_suggestions) {
    table_.query_suggestions(table_.self, request_id, ToKr(text));
  }
}

void BrowserPort::AcceptSuggestion(std::uint32_t request_id,
                                   std::size_t index,
                                   Disposition disposition) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, accept_suggestion) &&
      table_.accept_suggestion) {
    table_.accept_suggestion(table_.self, request_id, index,
                             std::to_underlying(disposition));
  }
}

void BrowserPort::InvokeAction(ActionId action, Rect anchor) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, invoke_action) && table_.invoke_action) {
    table_.invoke_action(table_.self, action.value, ToKr(anchor));
  }
}

void BrowserPort::RequestMenu(std::uint32_t request_id,
                              MenuKind kind,
                              std::uint64_t target,
                              Rect anchor) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, request_menu) && table_.request_menu) {
    table_.request_menu(table_.self, request_id, std::to_underlying(kind),
                        target, ToKr(anchor));
  }
}

void BrowserPort::MenuCommand(std::uint32_t request_id,
                              std::int32_t command_id) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, menu_command) && table_.menu_command)
    table_.menu_command(table_.self, request_id, command_id);
}

void BrowserPort::MenuClosed(std::uint32_t request_id) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, menu_closed) && table_.menu_closed)
    table_.menu_closed(table_.self, request_id);
}

void BrowserPort::SendWindowCommand(kr::WindowCommand command) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, window_command) &&
      table_.window_command) {
    table_.window_command(table_.self, std::to_underlying(command));
  }
}

void BrowserPort::SetWindowBackground(Color start, Color end) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, set_window_background) &&
      table_.set_window_background)
    table_.set_window_background(table_.self, start.argb, end.argb);
}

void BrowserPort::SetContentBounds(Rect bounds, float corner_radius) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, set_content_bounds) &&
      table_.set_content_bounds) {
    table_.set_content_bounds(table_.self, ToKr(bounds), corner_radius);
  }
}

void BrowserPort::SetAnchor(const Anchor& anchor) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, set_anchor) && table_.set_anchor) {
    table_.set_anchor(table_.self, std::to_underlying(anchor.kind), anchor.id,
                      ToKr(anchor.bounds), anchor.visible);
  }
}

void BrowserPort::SetDragRegions(std::span<const Rect> regions) const {
  if (!KR_HAS_SLOT(&table_, KrBrowser, set_drag_regions) ||
      !table_.set_drag_regions)
    return;
  // At most a handful of regions; convert on the stack.
  std::array<KrRect, 8> converted{};
  const std::size_t count = std::min(regions.size(), converted.size());
  std::ranges::transform(regions.first(count), converted.begin(),
                         [](const Rect& r) { return ToKr(r); });
  table_.set_drag_regions(table_.self, converted.data(), count);
}

void BrowserPort::SetLayoutPrefs(const LayoutPrefs& prefs) const {
  if (!KR_HAS_SLOT(&table_, KrBrowser, set_layout_prefs) ||
      !table_.set_layout_prefs)
    return;
  const KrLayoutPrefs converted{sizeof(KrLayoutPrefs), prefs.flags.bits(),
                                prefs.sidebar_width};
  table_.set_layout_prefs(table_.self, &converted);
}

void BrowserPort::FocusPage() const {
  if (KR_HAS_SLOT(&table_, KrBrowser, focus_page) && table_.focus_page)
    table_.focus_page(table_.self);
}
bool BrowserPort::UnhandledKey(const void* native_message) const {
  return native_message && KR_HAS_SLOT(&table_, KrBrowser, unhandled_key) &&
         table_.unhandled_key &&
         table_.unhandled_key(table_.self, native_message);
}

void BrowserPort::Log(LogLevel level, std::string_view text) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, log) && table_.log) {
    table_.log(table_.self, static_cast<std::uint32_t>(level), ToKr(text));
  }
}

}  // namespace kr
