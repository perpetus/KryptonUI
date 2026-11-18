#include <algorithm>
#include <utility>

#include "krypton/ui/model/address.hpp"
#include "krypton/ui/shell/shell.hpp"

namespace kr {
namespace {
std::size_t ByteOffset(std::string_view text, std::uint32_t utf16) {
  std::size_t offset = 0;
  std::uint32_t units = 0;
  while (offset < text.size() && units < utf16) {
    const auto byte = static_cast<unsigned char>(text[offset]);
    const std::size_t count = byte < 0x80   ? 1
                              : byte < 0xe0 ? 2
                              : byte < 0xf0 ? 3
                                            : 4;
    if (count > text.size() - offset || units + (count == 4 ? 2u : 1u) > utf16)
      break;
    offset += count;
    units += count == 4 ? 2 : 1;
  }
  return offset;
}
}  // namespace

void Shell::ToggleDictation() {
  if (dictation_request_) {
    StopDictation();
    return;
  }
  const auto& ui = store_.ui();
  if (!browser_.CanDictate() || !ui.command_bar.open || !ui.address_focused ||
      ui.command_bar.selection.composing)
    return;
  dictation_request_ = ++next_dictation_request_;
  if (!dictation_request_)
    dictation_request_ = ++next_dictation_request_;
  dictation_pending_focus_ = true;
  if (store_.ui().address_focused)
    BeginDictation();
}
void Shell::BeginDictation() {
  if (!dictation_request_ || !dictation_pending_focus_)
    return;
  const auto& bar = store_.ui().command_bar;
  if (!bar.open || !store_.ui().address_focused || bar.selection.composing) {
    StopDictation();
    return;
  }
  dictation_pending_focus_ = false;
  dictation_edit_request_ = bar.request_id;
  dictation_edit_revision_ = bar.edit_revision;
  const auto start =
      ByteOffset(bar.text, std::min(bar.selection.start, bar.selection.end));
  const auto end =
      ByteOffset(bar.text, std::max(bar.selection.start, bar.selection.end));
  dictation_prefix_ = bar.text.substr(0, start);
  dictation_suffix_ = bar.text.substr(end);
  CancelAddressQuery();
  store_.SetSuggestions(bar.request_id, {});
  ScheduleRender();
  browser_.StartDictation(dictation_request_);
}
void Shell::StopDictation() {
  const auto request = std::exchange(dictation_request_, 0);
  dictation_pending_focus_ = false;
  dictation_prefix_.clear();
  dictation_suffix_.clear();
  if (request)
    browser_.StopDictation(request);
}
Result<void> Shell::SetDictation(std::uint32_t request,
                                 std::uint32_t state,
                                 std::uint32_t flags,
                                 std::string_view text,
                                 std::string_view error) {
  if (state > KR_DICTATION_ERROR || (flags & ~KR_DICTATION_FINAL))
    return Fail(ErrorCode::kInvalidArgument, "Invalid dictation state");
  if (!request || request != dictation_request_)
    return {};
  const auto& bar = store_.ui().command_bar;
  if (!bar.open || !store_.ui().address_focused || bar.selection.composing ||
      bar.request_id != dictation_edit_request_ ||
      bar.edit_revision != dictation_edit_revision_) {
    StopDictation();
    return {};
  }
  if (!text.empty()) {
    std::string value = dictation_prefix_;
    value.append(text);
    const auto caret = AddressTextLength(value);
    value.append(dictation_suffix_);
    if (!caret || !AddressTextLength(value))
      return Fail(ErrorCode::kInvalidArgument, "Invalid dictation text");
    store_.SetCommandEdit(std::move(value), {*caret, *caret, false}, false);
    ScheduleRender();
    // Updating the edit does not request suggestions or navigate. Native input
    // and explicit acceptance retain their ordinary paths.
    NotifyAddressState();
  }
  if (state != KR_DICTATION_LISTENING) {
    StopDictation();
    if (!error.empty())
      std::ignore = ShowNotification(error);
  }
  return {};
}
}  // namespace kr
