#include <algorithm>
#include <limits>
#include <utility>

#include "krypton/ui/model/address_key.hpp"
#include "krypton/ui/shell/shell.hpp"

namespace kr {
bool Shell::OnAddressKey(AddressKey key, Flags<AddressModifier> modifiers) {
  if (!store_.ui().command_bar.open ||
      store_.ui().command_bar.selection.composing ||
      RouteAddressKey(key, modifiers) != AddressKeyRoute::kAddress)
    return false;
  return browser_.HandleAddressKey(key, modifiers);
}
Result<void> Shell::SetSuggestionSelection(std::uint32_t request,
                                           std::uint64_t generation,
                                           std::size_t index) {
  const auto& bar = store_.ui().command_bar;
  if (!bar.open || request != bar.request_id || generation != bar.generation)
    return {};
  const bool none = index == std::numeric_limits<std::size_t>::max();
  if (!none && index >= bar.suggestions.size())
    return Fail(ErrorCode::kInvalidArgument, "Invalid suggestion selection");
  store_.SetCommandSelection(index);
  if (request == rendered_request_ && generation == rendered_generation_)
    rendered_selection_ = none ? -1 : static_cast<int>(index);
  ScheduleRender();
  return {};
}
void Shell::NotifyAddressState() {
  const auto& bar = store_.ui().command_bar;
  Flags<AddressFlag> flags;
  flags.Set(AddressFlag::kOpen, bar.open)
      .Set(AddressFlag::kFocused, bar.open && store_.ui().address_focused)
      .Set(AddressFlag::kNewTab, bar.open && command_opens_new_tab_);
  browser_.AddressStateChanged(
      flags, bar.request_id,
      bar.open ? std::string_view(bar.text) : std::string_view{},
      bar.open ? bar.selection : AddressSelection{}, bar.edit_revision);
}

void Shell::CancelAddressQuery() {
  browser_.CancelSuggestions(store_.ui().command_bar.request_id);
}

void Shell::CloseAddress() {
  if (!store_.ui().command_bar.open)
    return;
  const auto request = store_.ui().command_bar.request_id;
  store_.CloseCommandBar();
  store_.SetAddressFocused(false);
  command_opens_new_tab_ = false;
  ScheduleRender();
  browser_.CancelSuggestions(request);
  NotifyAddressState();
}

void Shell::DismissAddress() {
  CloseAddress();
}

Result<void> Shell::FocusNative(Anchor target) {
  if (target.kind == AnchorKind::kElement) {
    if (target.id < std::to_underlying(Element::kAddress) ||
        target.id > std::to_underlying(Element::kCompactMode))
      return Fail(ErrorCode::kInvalidArgument, "Unknown focus element");
  } else if (target.kind == AnchorKind::kTab) {
    const auto* tab = store_.FindTab(TabId{target.id});
    if (!tab || tab->space != store_.active_space())
      return Fail(ErrorCode::kInvalidArgument, "Unavailable focus tab");
  } else if (target.kind == AnchorKind::kAction) {
    const auto& actions = store_.actions();
    if (std::ranges::none_of(actions, [&](const Action& action) {
          return action.id == ActionId{target.id};
        }))
      return Fail(ErrorCode::kInvalidArgument, "Unknown focus action");
  } else {
    return Fail(ErrorCode::kInvalidArgument, "Unknown focus target");
  }
  CloseAddress();
  store_.SetSidebarRevealed(true);
  store_.SetToolbarRevealed(true);
  ScheduleRender();
  const auto serial = ++native_focus_serial_;
  view_->PostTask(
      [this, alive = std::weak_ptr<int>(lifetime_), serial, target] {
        if (!alive.expired() && serial == native_focus_serial_)
          view_->FocusNative(target);
      });
  return {};
}

Result<void> Shell::SetAddressEdit(std::uint32_t request,
                                   std::uint64_t revision,
                                   std::string text,
                                   AddressSelection selection) {
  if (!AddressTextLength(text, selection))
    return Fail(ErrorCode::kInvalidArgument, "Invalid address edit selection");
  const auto& bar = store_.ui().command_bar;
  if (!bar.open || request != bar.request_id || revision != bar.edit_revision ||
      bar.selection.composing)
    return {};
  if (text == bar.text && selection == bar.selection)
    return {};
  store_.SetCommandEdit(std::move(text), selection, false);
  ScheduleRender();
  return {};
}

void Shell::SetAddressText(std::string text, bool select_all) {
  OnWorkspaceEditorDismissed();
  OnThemeEditorDismissed();
  OnTabPreviewDismissed(store_.ui().tab_preview.request_id);
  CancelAddressQuery();
  store_.OpenCommandBar(std::move(text), select_all);
  const auto request = store_.SetCommandText(store_.ui().command_bar.text);
  // SetCommandText clears selection intent, but a host-requested replacement
  // should still select the input when requested.
  store_.SetCommandSelectAll(select_all);
  ScheduleRender();
  NotifyAddressState();
  browser_.QuerySuggestions(request, store_.ui().command_bar.text);
}

Result<void> Shell::SetSuggestionSnapshot(std::uint32_t request,
                                          std::uint64_t generation,
                                          std::vector<Suggestion> items) {
  if (!generation || !browser_.SupportsSuggestionSnapshots())
    return Fail(ErrorCode::kInvalidArgument, "Unsupported suggestion snapshot");
  const auto& bar = store_.ui().command_bar;
  if (!bar.open || request != bar.request_id || generation <= bar.generation)
    return {};
  store_.SetSuggestions(request, std::move(items), generation);
  ScheduleRender();
  return {};
}

}  // namespace kr
