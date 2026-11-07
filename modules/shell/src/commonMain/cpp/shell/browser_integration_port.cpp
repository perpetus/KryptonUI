#include <utility>

#include "krypton/ui/shell/shell.hpp"

namespace kr {
static_assert(std::to_underlying(AddressModifier::kShift) ==
              KR_ADDRESS_KEY_SHIFT);
static_assert(std::to_underlying(AddressModifier::kControl) ==
              KR_ADDRESS_KEY_CTRL);
static_assert(std::to_underlying(AddressKey::kControl) ==
              KR_ADDRESS_KEY_CONTROL);
bool BrowserPort::HandleAddressKey(AddressKey key,
                                   Flags<AddressModifier> modifiers) const {
  return KR_HAS_SLOT(&table_, KrBrowser, handle_address_key) &&
         table_.handle_address_key &&
         table_.handle_address_key(table_.self, std::to_underlying(key),
                                   modifiers.bits());
}
static_assert(std::to_underlying(AddressFlag::kOpen) == KR_ADDRESS_OPEN);
static_assert(std::to_underlying(AddressFlag::kFocused) == KR_ADDRESS_FOCUSED);
static_assert(std::to_underlying(AddressFlag::kNewTab) == KR_ADDRESS_NEW_TAB);
static_assert(std::to_underlying(WorkspaceEditKind::kUpsert) ==
              KR_WORKSPACE_UPSERT);
static_assert(std::to_underlying(WorkspaceEditKind::kRemove) ==
              KR_WORKSPACE_REMOVE);
namespace {
KrStr Borrow(std::string_view value) noexcept {
  return {value.data(), value.size()};
}
}  // namespace

void BrowserPort::CancelSuggestions(std::uint32_t request) const {
  if (request && KR_HAS_SLOT(&table_, KrBrowser, cancel_suggestions) &&
      table_.cancel_suggestions)
    table_.cancel_suggestions(table_.self, request);
}

void BrowserPort::AddressStateChanged(Flags<AddressFlag> flags,
                                      std::uint32_t request,
                                      std::string_view text,
                                      AddressSelection selection,
                                      std::uint64_t revision) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, address_state_changed) &&
      table_.address_state_changed) {
    const KrAddressState state{
        sizeof(KrAddressState), flags.bits(),  request,  Borrow(text),
        selection.start,        selection.end, revision, selection.composing};
    table_.address_state_changed(table_.self, &state);
  }
}

bool BrowserPort::SupportsSuggestionSnapshots() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, accept_suggestion_snapshot) &&
         table_.accept_suggestion_snapshot;
}

void BrowserPort::SelectSuggestionSnapshot(std::uint32_t request,
                                           std::uint64_t generation,
                                           std::size_t index) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, select_suggestion_snapshot) &&
      table_.select_suggestion_snapshot)
    table_.select_suggestion_snapshot(table_.self, request, generation, index);
}

void BrowserPort::AcceptSuggestionSnapshot(std::uint32_t request,
                                           std::uint64_t generation,
                                           std::size_t index,
                                           Disposition disposition) const {
  if (SupportsSuggestionSnapshots())
    table_.accept_suggestion_snapshot(table_.self, request, generation, index,
                                      std::to_underlying(disposition));
}

bool BrowserPort::CanSetTabIcon() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, set_tab_icon) && table_.set_tab_icon;
}
void BrowserPort::SetTabIcon(TabId tab, std::string_view glyph) const {
  if (CanSetTabIcon())
    table_.set_tab_icon(table_.self, tab.value, {glyph.data(), glyph.size()});
}
bool BrowserPort::CanEditWorkspaces() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, edit_workspace) &&
         table_.edit_workspace;
}

void BrowserPort::EditWorkspace(WorkspaceEditKind kind,
                                const Space& space) const {
  if (!CanEditWorkspaces())
    return;
  const KrSpace value{sizeof(KrSpace), space.id.value, Borrow(space.name),
                      Borrow(space.glyph), space.color.argb};
  table_.edit_workspace(table_.self, std::to_underlying(kind), &value);
}

void BrowserPort::SetShellMetrics(float width, float height) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, set_shell_metrics) &&
      table_.set_shell_metrics) {
    const KrShellMetrics value{sizeof(KrShellMetrics), width, height};
    table_.set_shell_metrics(table_.self, &value);
  }
}

}  // namespace kr
