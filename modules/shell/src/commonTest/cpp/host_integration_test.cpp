#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton/ui/model/address_key.hpp"
#include "krypton/ui/presentation/presenter.hpp"
#include "krypton/ui/shell/shell.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {
KrStr Borrow(std::string_view value) {
  return {value.data(), value.size()};
}

struct IntegrationHost {
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};
  std::uint32_t request = 0, accepted_request = 0, address_flags = 0;
  std::uint64_t accepted_generation = 0;
  std::uint64_t selected_generation = 0;
  std::size_t selected_index = 0;
  int selection_notices = 0;
  std::uint64_t edit_revision = 0;
  AddressSelection selection;
  int address_notices = 0, query_count = 0;
  int address_keys = 0;
  std::size_t accepted_index = 0;
  std::vector<std::uint32_t> cancelled;
  bool resize_on_metrics = false;
  std::vector<float> content_rights;
  std::string text, navigation, workspace_name, workspace_glyph;
  std::vector<std::string> events;
  int closed_menus = 0, metrics_calls = 0, backdrop_calls = 0,
      workspace_edits = 0;
  KrSpaceId workspace_id = 0;
  std::uint32_t edit_kind = 0, menu = 0;
  KrShellMetrics metrics{};
  KrWindowBackdrop backdrop{};
  std::array<KrGradientLayer, 3> layers{};
  explicit IntegrationHost(bool workspace_support = true,
                           bool selection_support = true) {
    headless::Reset();
    KrBrowser browser{.struct_size = sizeof(KrBrowser), .self = this};
    browser.query_suggestions = [](void* self, std::uint32_t request, KrStr) {
      static_cast<IntegrationHost*>(self)->request = request;
      ++static_cast<IntegrationHost*>(self)->query_count;
    };
    browser.address_state_changed = [](void* self,
                                       const KrAddressState* value) {
      auto& host = *static_cast<IntegrationHost*>(self);
      host.address_flags = value->flags;
      ++host.address_notices;
      host.edit_revision = value->edit_revision;
      host.selection = {value->selection_start, value->selection_end,
                        value->composing};
      host.text = value->text.size
                      ? std::string(value->text.data, value->text.size)
                      : std::string{};
    };
    browser.cancel_suggestions = [](void* self, std::uint32_t request) {
      auto& host = *static_cast<IntegrationHost*>(self);
      host.cancelled.push_back(request);
      host.events.emplace_back("cancel");
    };
    browser.handle_address_key = [](void* self, std::uint32_t, std::uint32_t) {
      ++static_cast<IntegrationHost*>(self)->address_keys;
      return true;
    };
    browser.accept_suggestion_snapshot = [](void* self, std::uint32_t request,
                                            std::uint64_t generation,
                                            std::size_t index, std::uint32_t) {
      auto& host = *static_cast<IntegrationHost*>(self);
      host.accepted_request = request;
      host.accepted_generation = generation;
      host.accepted_index = index;
      host.events.emplace_back("accept");
    };
    browser.select_suggestion_snapshot = [](void* self, std::uint32_t request,
                                            std::uint64_t generation,
                                            std::size_t index) {
      auto& host = *static_cast<IntegrationHost*>(self);
      KR_EXPECT_EQ(request, host.request);
      host.selected_generation = generation;
      host.selected_index = index;
      ++host.selection_notices;
    };
    if (!selection_support)
      browser.struct_size = offsetof(KrBrowser, select_suggestion_snapshot);
    browser.navigate = [](void* self, KrStr text, std::uint32_t) {
      static_cast<IntegrationHost*>(self)->navigation.assign(text.data,
                                                             text.size);
    };
    if (workspace_support)
      browser.edit_workspace = [](void* self, std::uint32_t kind,
                                  const KrSpace* value) {
        auto& host = *static_cast<IntegrationHost*>(self);
        ++host.workspace_edits;
        host.workspace_id = value->id;
        host.edit_kind = kind;
        host.workspace_name.assign(value->name.data, value->name.size);
        host.workspace_glyph.assign(value->glyph.data, value->glyph.size);
      };
    browser.request_menu = [](void* self, std::uint32_t request, std::uint32_t,
                              std::uint64_t, KrRect) {
      static_cast<IntegrationHost*>(self)->menu = request;
    };
    browser.menu_closed = [](void* self, std::uint32_t) {
      ++static_cast<IntegrationHost*>(self)->closed_menus;
    };
    browser.set_shell_metrics = [](void* self, const KrShellMetrics* value) {
      auto& host = *static_cast<IntegrationHost*>(self);
      ++host.metrics_calls;
      host.metrics = *value;
      if (std::exchange(host.resize_on_metrics, false))
        host.Window(1200, 750);
    };
    browser.set_content_bounds = [](void* self, KrRect rect, float) {
      static_cast<IntegrationHost*>(self)->content_rights.push_back(rect.x +
                                                                    rect.width);
    };
    browser.set_window_backdrop = [](void* self,
                                     const KrWindowBackdrop* value) {
      auto& host = *static_cast<IntegrationHost*>(self);
      ++host.backdrop_calls;
      host.backdrop = *value;
      for (std::size_t index = 0; index < value->layer_count; ++index)
        host.layers[index] = value->layers[index];
      host.backdrop.layers = host.layers.data();
      // The host would copy grain pixels during the callback. Tests below
      // inspect only metadata; never retain the borrowed pixel pointer.
      host.backdrop.grain.pixels = nullptr;
    };
    const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                                &browser};
    KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
    const std::array spaces{
        KrSpace{sizeof(KrSpace), 1, Borrow("Default"), Borrow("D"), 0xFF887766},
        KrSpace{sizeof(KrSpace), 2, Borrow("Work"), Borrow("W"), 0xFF668877}};
    ui.set_spaces(ui.self, spaces.data(), spaces.size(), sizeof(KrSpace), 1);
    Window(900, 600);
    headless::RunPendingTasks();
  }
  ~IntegrationHost() {
    ui.destroy(ui.self);
    headless::RunPendingTasks();
  }
  IntegrationHost(const IntegrationHost&) = delete;
  IntegrationHost& operator=(const IntegrationHost&) = delete;
  void Window(float width, float height, float scale = 1) {
    const KrWindowState state{
        sizeof(state), KR_WINDOW_ACTIVE, scale, {0, 0, width, height}};
    ui.set_window_state(ui.self, &state);
  }
  void Snapshot(std::uint64_t generation,
                std::string_view label,
                std::uint32_t query = 0) {
    const KrSuggestion item{sizeof(item),
                            KR_SUGGESTION_URL,
                            KR_SUGGESTION_DEFAULT,
                            Borrow(label),
                            {},
                            0};
    KR_EXPECT_EQ(ui.set_suggestion_snapshot(ui.self, query ? query : request,
                                            generation, &item, 1, sizeof(item)),
                 KR_OK);
  }
};

KR_TEST(NativeFocusRunsAfterRenderAndLatestRequestWins) {
  IntegrationHost host;
  host.ui.set_address_text(host.ui.self, Borrow("editing"), true);
  const KrNativeFocus toolbar{sizeof(toolbar), KR_ANCHOR_ELEMENT,
                              KR_ELEMENT_TOOLBAR};
  const KrNativeFocus menu{sizeof(menu), KR_ANCHOR_ELEMENT,
                           KR_ELEMENT_APP_MENU};
  KR_EXPECT_EQ(host.ui.focus_native(host.ui.self, &toolbar), KR_OK);
  KR_EXPECT_EQ(host.ui.focus_native(host.ui.self, &menu), KR_OK);
  KR_EXPECT(!headless::LastRender().focused_native);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().command_bar_open);
  KR_ASSERT(headless::LastRender().focused_native.has_value());
  KR_EXPECT_EQ(headless::LastRender().focused_native->id,
               static_cast<std::uint64_t>(KR_ELEMENT_APP_MENU));
  KR_EXPECT(host.navigation.empty());
}

KR_TEST(NativeFocusRejectsUnavailableAndMalformedTargets) {
  IntegrationHost host;
  KrNativeFocus target{sizeof(target), KR_ANCHOR_ELEMENT, 999};
  KR_EXPECT_EQ(host.ui.focus_native(host.ui.self, &target),
               KR_ERR_INVALID_ARGUMENT);
  target = {sizeof(target), KR_ANCHOR_TAB, 999};
  KR_EXPECT_EQ(host.ui.focus_native(host.ui.self, &target),
               KR_ERR_INVALID_ARGUMENT);
  target = {sizeof(target), KR_ANCHOR_ACTION, 999};
  KR_EXPECT_EQ(host.ui.focus_native(host.ui.self, &target),
               KR_ERR_INVALID_ARGUMENT);
  target.struct_size = offsetof(KrNativeFocus, id);
  KR_EXPECT_EQ(host.ui.focus_native(host.ui.self, &target),
               KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT_EQ(host.ui.focus_native(host.ui.self, nullptr),
               KR_ERR_INVALID_ARGUMENT);
}

KR_TEST(AddressEditSessionReportsFocusAndCancelsExactlyOnce) {
  IntegrationHost host;
  std::string text = "example.test";
  KR_EXPECT_EQ(host.ui.set_address_text(host.ui.self, Borrow(text), true),
               KR_OK);
  text.clear();
  const auto first = host.request;
  KR_EXPECT(first != 0);
  KR_EXPECT_EQ(host.text, "example.test");
  KR_EXPECT((host.address_flags & KR_ADDRESS_OPEN) != 0);
  headless::Delegate()->OnAddressFocusChanged(true);
  KR_EXPECT((host.address_flags & KR_ADDRESS_FOCUSED) != 0);
  headless::Delegate()->OnCommandTextChanged("next.test");
  KR_EXPECT_EQ(host.cancelled.size(), std::size_t{1});
  KR_EXPECT_EQ(host.cancelled[0], first);
  const auto second = host.request;
  host.ui.dismiss_address(host.ui.self);
  host.ui.dismiss_address(host.ui.self);
  KR_EXPECT_EQ(host.cancelled.size(), std::size_t{2});
  KR_EXPECT_EQ(host.cancelled.back(), second);
  KR_EXPECT_EQ(host.address_flags, std::uint32_t{0});
}

KR_TEST(AddressBrowserShortcutsDoNotChangeAutocompleteSelection) {
  IntegrationHost host;
  host.ui.set_address_text(host.ui.self, Borrow("draft"), false);
  host.Snapshot(1, "suggestion");
  headless::RunPendingTasks();
  for (const auto key : {AddressKey::kTab, AddressKey::kPageUp,
                         AddressKey::kPageDown, AddressKey::kDeleteMatch}) {
    for (const auto modifiers :
         {Flags<AddressModifier>{AddressModifier::kControl},
          AddressModifier::kControl | AddressModifier::kShift}) {
      KR_EXPECT(RouteAddressKey(key, modifiers) == AddressKeyRoute::kBrowser);
      KR_EXPECT(!headless::Delegate()->OnAddressKey(key, modifiers));
    }
  }
  KR_EXPECT_EQ(host.address_keys, 0);
  KR_EXPECT_EQ(host.selection_notices, 0);
  KR_EXPECT_EQ(host.query_count, 1);
  KR_EXPECT_EQ(host.text, "draft");
  KR_EXPECT((host.address_flags & KR_ADDRESS_OPEN) != 0);
}

KR_TEST(AddressShiftArrowsStayWithTheTextEditor) {
  IntegrationHost host;
  host.ui.set_address_text(host.ui.self, Borrow("selection"), false);
  for (const auto key : {AddressKey::kUp, AddressKey::kDown,
                         AddressKey::kPageUp, AddressKey::kPageDown}) {
    KR_EXPECT(RouteAddressKey(key, AddressModifier::kShift) ==
              AddressKeyRoute::kText);
    KR_EXPECT(
        !headless::Delegate()->OnAddressKey(key, AddressModifier::kShift));
  }
  KR_EXPECT_EQ(host.address_keys, 0);
  KR_EXPECT_EQ(host.selection_notices, 0);
  KR_EXPECT_EQ(host.text, "selection");
}

KR_TEST(AddressNavigationStillReachesTheHostAndLeavesImeAlone) {
  IntegrationHost host;
  host.ui.set_address_text(host.ui.self, Borrow("draft"), false);
  for (const auto key :
       {AddressKey::kDown, AddressKey::kUp, AddressKey::kPageDown,
        AddressKey::kPageUp, AddressKey::kTab, AddressKey::kEscape})
    KR_EXPECT(headless::Delegate()->OnAddressKey(key, {}));
  KR_EXPECT(headless::Delegate()->OnAddressKey(AddressKey::kTab,
                                               AddressModifier::kShift));
  KR_EXPECT(headless::Delegate()->OnAddressKey(AddressKey::kDeleteMatch,
                                               AddressModifier::kShift));
  KR_EXPECT_EQ(host.address_keys, 8);
  headless::Delegate()->OnAddressEditChanged("draft", {5, 5, true});
  KR_EXPECT(!headless::Delegate()->OnAddressKey(AddressKey::kDown, {}));
  KR_EXPECT_EQ(host.address_keys, 8);
}

KR_TEST(AddressModelEditsPreserveSelectionWithoutQueryOrFeedback) {
  IntegrationHost host;
  host.ui.set_address_text(host.ui.self, Borrow("native"), true);
  const auto query = host.request;
  const auto notices = host.address_notices;
  const auto revision = host.edit_revision;
  std::string text = "https://example.test";
  const KrAddressEdit edit{sizeof(KrAddressEdit), query, revision,
                           Borrow(text),          8,     15};
  KR_EXPECT_EQ(host.ui.set_address_edit(host.ui.self, &edit), KR_OK);
  text.clear();
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::LastRender().command_text, "https://example.test");
  KR_EXPECT_EQ(headless::LastRender().address_selection.start,
               std::uint32_t{8});
  KR_EXPECT_EQ(headless::LastRender().address_selection.end, std::uint32_t{15});
  KR_EXPECT_EQ(host.request, query);
  KR_EXPECT_EQ(host.query_count, 1);
  KR_EXPECT_EQ(host.address_notices, notices);
}

KR_TEST(AddressNativeSelectionAndCompositionUseUtf16Revisions) {
  IntegrationHost host;
  const std::string text = "a\xF0\x9F\x98\x80z";
  host.ui.set_address_text(host.ui.self, Borrow(text), true);
  KR_EXPECT_EQ(host.selection.start, std::uint32_t{0});
  KR_EXPECT_EQ(host.selection.end, std::uint32_t{4});
  const auto revision = host.edit_revision;
  headless::Delegate()->OnAddressEditChanged(text, {1, 3, false});
  KR_EXPECT_EQ(host.query_count, 1);
  KR_EXPECT(host.edit_revision > revision);
  KR_EXPECT_EQ(host.selection.start, std::uint32_t{1});
  headless::Delegate()->OnAddressEditChanged("composing", {9, 9, true});
  KR_EXPECT(host.selection.composing);
  KR_EXPECT_EQ(host.query_count, 1);
  const KrAddressEdit edit{sizeof(KrAddressEdit),
                           host.request,
                           host.edit_revision,
                           Borrow("wrong"),
                           5,
                           5};
  KR_EXPECT_EQ(host.ui.set_address_edit(host.ui.self, &edit), KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::LastRender().command_text, "composing");
  headless::Delegate()->OnAddressEditChanged("composing", {9, 9, false});
  KR_EXPECT(!host.selection.composing);
  KR_EXPECT_EQ(host.query_count, 2);
}

KR_TEST(AddressModelEditsIgnoreStaleRequestsAndNativeRevisions) {
  IntegrationHost host;
  host.ui.set_address_text(host.ui.self, Borrow("initial"), true);
  KrAddressEdit edit{sizeof(KrAddressEdit),
                     host.request,
                     host.edit_revision,
                     Borrow("stale"),
                     0,
                     5};
  headless::Delegate()->OnAddressEditChanged("initial", {2, 2, false});
  KR_EXPECT_EQ(host.ui.set_address_edit(host.ui.self, &edit), KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::LastRender().command_text, "initial");
  edit.edit_revision = host.edit_revision;
  headless::Delegate()->OnCommandTextChanged("new input");
  KR_EXPECT_EQ(host.ui.set_address_edit(host.ui.self, &edit), KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::LastRender().command_text, "new input");
}

KR_TEST(AddressModelEditsValidateSizedPayloadAndSurrogateBoundaries) {
  IntegrationHost host;
  host.ui.set_address_text(host.ui.self, Borrow("input"), true);
  KrAddressEdit edit{sizeof(KrAddressEdit),
                     host.request,
                     host.edit_revision,
                     Borrow("a\xF0\x9F\x98\x80z"),
                     2,
                     2};
  KR_EXPECT_EQ(host.ui.set_address_edit(host.ui.self, &edit),
               KR_ERR_INVALID_ARGUMENT);
  edit.selection_start = 0;
  edit.selection_end = 5;
  KR_EXPECT_EQ(host.ui.set_address_edit(host.ui.self, &edit),
               KR_ERR_INVALID_ARGUMENT);
  edit.selection_end = 4;
  edit.text = Borrow("\xED\xA0\x80");
  KR_EXPECT_EQ(host.ui.set_address_edit(host.ui.self, &edit),
               KR_ERR_INVALID_ARGUMENT);
  edit.text = {nullptr, 3};
  KR_EXPECT_EQ(host.ui.set_address_edit(host.ui.self, &edit),
               KR_ERR_INVALID_ARGUMENT);
  edit.struct_size = offsetof(KrAddressEdit, selection_end);
  KR_EXPECT_EQ(host.ui.set_address_edit(host.ui.self, &edit),
               KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT_EQ(host.ui.set_address_edit(host.ui.self, nullptr),
               KR_ERR_INVALID_ARGUMENT);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::LastRender().command_text, "input");
}

KR_TEST(SuggestionSelectionAndAcceptanceUseTheDisplayedGeneration) {
  IntegrationHost host;
  host.ui.set_address_text(host.ui.self, Borrow("query"), true);
  const std::array old{KrSuggestion{sizeof(KrSuggestion),
                                    KR_SUGGESTION_SEARCH,
                                    0,
                                    Borrow("first"),
                                    {},
                                    0},
                       KrSuggestion{sizeof(KrSuggestion),
                                    KR_SUGGESTION_SEARCH,
                                    0,
                                    Borrow("second"),
                                    {},
                                    0}};
  KR_EXPECT_EQ(
      host.ui.set_suggestion_snapshot(host.ui.self, host.request, 1, old.data(),
                                      old.size(), sizeof(KrSuggestion)),
      KR_OK);
  headless::RunPendingTasks();
  host.Snapshot(2, "not rendered yet");
  headless::Delegate()->OnCommandSelectionMoved(1);
  KR_EXPECT_EQ(host.selected_generation, std::uint64_t{1});
  KR_EXPECT_EQ(host.selected_index, std::size_t{1});
  KR_EXPECT(host.navigation.empty());
  KR_EXPECT_EQ(host.query_count, 1);
  headless::Delegate()->OnCommandAccepted(Disposition::kCurrentTab);
  KR_EXPECT_EQ(host.accepted_generation, std::uint64_t{1});
  KR_EXPECT_EQ(host.accepted_index, std::size_t{1});
}

KR_TEST(SuggestionKeyboardSelectionWrapsWithoutQueryingOrAccepting) {
  IntegrationHost host;
  host.ui.set_address_text(host.ui.self, Borrow("query"), true);
  host.Snapshot(1, "only match");
  headless::RunPendingTasks();
  headless::Delegate()->OnCommandSelectionMoved(-1);
  headless::Delegate()->OnCommandSelectionMoved(1);
  KR_EXPECT_EQ(host.selection_notices, 2);
  KR_EXPECT_EQ(host.selected_index, std::size_t{0});
  KR_EXPECT_EQ(host.query_count, 1);
  KR_EXPECT_EQ(host.accepted_generation, std::uint64_t{0});
  headless::Delegate()->OnAddressEditChanged("query", {5, 5, true});
  headless::Delegate()->OnCommandSelectionMoved(1);
  KR_EXPECT_EQ(host.selection_notices, 2);
}

KR_TEST(SuggestionKeyboardSelectionSupportsOlderHostTables) {
  IntegrationHost host(true, false);
  host.ui.set_address_text(host.ui.self, Borrow("query"), true);
  const std::array items{KrSuggestion{sizeof(KrSuggestion),
                                      KR_SUGGESTION_SEARCH,
                                      0,
                                      Borrow("first"),
                                      {},
                                      0},
                         KrSuggestion{sizeof(KrSuggestion),
                                      KR_SUGGESTION_SEARCH,
                                      0,
                                      Borrow("second"),
                                      {},
                                      0}};
  KR_EXPECT_EQ(host.ui.set_suggestion_snapshot(host.ui.self, host.request, 1,
                                               items.data(), items.size(),
                                               sizeof(KrSuggestion)),
               KR_OK);
  headless::RunPendingTasks();
  headless::Delegate()->OnCommandSelectionMoved(1);
  KR_EXPECT_EQ(host.selection_notices, 0);
  headless::Delegate()->OnCommandAccepted(Disposition::kCurrentTab);
  KR_EXPECT_EQ(host.accepted_generation, std::uint64_t{1});
  KR_EXPECT_EQ(host.accepted_index, std::size_t{1});
}

KR_TEST(SuggestionSnapshotsRejectOlderGenerationsAndQueries) {
  IntegrationHost host;
  host.ui.set_address_text(host.ui.self, Borrow("first"), false);
  const auto old = host.request;
  host.Snapshot(2, "current");
  host.Snapshot(1, "out of order");
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::LastRender().suggestions[0], "current");
  headless::Delegate()->OnCommandTextChanged("second");
  host.Snapshot(99, "previous query", old);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().suggestions.empty());
}

KR_TEST(SuggestionAcceptanceUsesDisplayedSnapshotBeforeCancelling) {
  IntegrationHost host;
  host.ui.set_address_text(host.ui.self, Borrow("query"), false);
  host.Snapshot(1, "displayed");
  headless::RunPendingTasks();
  host.Snapshot(2, "not rendered yet");
  headless::Delegate()->OnSuggestionChosen(0, Disposition::kCurrentTab);
  KR_EXPECT_EQ(host.accepted_generation, std::uint64_t{1});
  KR_EXPECT_EQ(host.accepted_request, host.request);
  KR_EXPECT(host.navigation.empty());
  KR_ASSERT(host.events.size() == 2);
  KR_EXPECT_EQ(host.events[0], "accept");
  KR_EXPECT_EQ(host.events[1], "cancel");
}

KR_TEST(ChangingTextCannotAcceptResultsFromThePreviousEditor) {
  IntegrationHost host;
  host.ui.set_address_text(host.ui.self, Borrow("old"), false);
  host.Snapshot(1, "old destination");
  headless::RunPendingTasks();
  headless::Delegate()->OnCommandTextChanged("new destination");
  headless::Delegate()->OnSuggestionChosen(0, Disposition::kCurrentTab);
  KR_EXPECT_EQ(host.accepted_generation, std::uint64_t{0});
  KR_EXPECT_EQ(host.navigation, "new destination");
}

KR_TEST(SuggestionSnapshotsCopyChromiumImagesAndUtf16Classifications) {
  IntegrationHost host;
  KR_ASSERT(host.ui.set_address_text(host.ui.self, Borrow("query"), true) ==
            KR_OK);
  std::string title = "A\xF0\x9F\x98\x80 result";
  std::array classes{
      KrSuggestionClassification{sizeof(KrSuggestionClassification), 0,
                                 KR_SUGGESTION_TEXT_MATCH},
      KrSuggestionClassification{sizeof(KrSuggestionClassification), 3,
                                 KR_SUGGESTION_TEXT_DIM}};
  KrSuggestion item{
      .struct_size = sizeof(KrSuggestion),
      .kind = KR_SUGGESTION_SEARCH,
      .flags = KR_SUGGESTION_DEFAULT | KR_SUGGESTION_RICH_IMAGE,
      .contents = Borrow(title),
      .icon = 77,
      .contents_class = classes.data(),
      .contents_class_count = classes.size(),
      .contents_class_stride = sizeof(KrSuggestionClassification)};
  KR_EXPECT_EQ(host.ui.set_suggestion_snapshot(host.ui.self, host.request, 1,
                                               &item, 1, sizeof(item)),
               KR_OK);
  title.assign("changed");
  classes[0].style = 0;
  const auto& suggestion = static_cast<Shell*>(host.ui.self)
                               ->store()
                               .ui()
                               .command_bar.suggestions[0];
  KR_EXPECT_EQ(suggestion.contents, "A\xF0\x9F\x98\x80 result");
  KR_EXPECT_EQ(suggestion.icon, ImageId{77});
  KR_EXPECT(suggestion.rich_image);
  KR_EXPECT(
      suggestion.contents_class[0].style.Has(SuggestionTextStyle::kMatch));
  Presenter presenter;
  const auto& vm = presenter.Present(static_cast<Shell*>(host.ui.self)->store(),
                                     {}, Dirty::kAll);
  KR_EXPECT(vm.command_bar.suggestions[0].rich_image);
  KR_EXPECT_EQ(vm.command_bar.suggestions[0].contents_class[1].offset, 3u);
}
KR_TEST(SuggestionClassificationsRejectSurrogateSplitsAndSupportOldPayloads) {
  IntegrationHost host;
  KR_ASSERT(host.ui.set_address_text(host.ui.self, Borrow("query"), true) ==
            KR_OK);
  std::array styles{
      KrSuggestionClassification{sizeof(KrSuggestionClassification), 0,
                                 KR_SUGGESTION_TEXT_MATCH},
      KrSuggestionClassification{sizeof(KrSuggestionClassification), 2,
                                 KR_SUGGESTION_TEXT_DIM}};
  KrSuggestion item{
      .struct_size = sizeof(item),
      .kind = KR_SUGGESTION_SEARCH,
      .contents = Borrow("A\xF0\x9F\x98\x80 result"),
      .contents_class = styles.data(),
      .contents_class_count = styles.size(),
      .contents_class_stride = sizeof(KrSuggestionClassification)};
  KR_EXPECT_EQ(host.ui.set_suggestion_snapshot(host.ui.self, host.request, 1,
                                               &item, 1, sizeof(item)),
               KR_ERR_INVALID_ARGUMENT);
  styles[1].offset = 3;
  item.contents_class_stride = 1;
  KR_EXPECT_EQ(host.ui.set_suggestion_snapshot(host.ui.self, host.request, 1,
                                               &item, 1, sizeof(item)),
               KR_ERR_INVALID_ARGUMENT);
  item.struct_size = offsetof(KrSuggestion, contents_class);
  KR_EXPECT_EQ(host.ui.set_suggestion_snapshot(host.ui.self, host.request, 1,
                                               &item, 1, sizeof(item)),
               KR_OK);
  const auto& suggestion = static_cast<Shell*>(host.ui.self)
                               ->store()
                               .ui()
                               .command_bar.suggestions[0];
  KR_EXPECT(suggestion.contents_class.empty());
  KR_EXPECT(!suggestion.rich_image);
}
KR_TEST(SuggestionSnapshotValidatesPayloadBeforeChangingState) {
  IntegrationHost host;
  host.ui.set_address_text(host.ui.self, Borrow("query"), false);
  host.Snapshot(1, "retained");
  KR_EXPECT_EQ(host.ui.set_suggestion_snapshot(host.ui.self, host.request, 0,
                                               nullptr, 0, 0),
               KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT_EQ(
      host.ui.set_suggestion_snapshot(host.ui.self, host.request, 2, nullptr, 1,
                                      sizeof(KrSuggestion)),
      KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT_EQ(host.ui.set_address_text(host.ui.self, {nullptr, 2}, false),
               KR_ERR_INVALID_ARGUMENT);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::LastRender().suggestions[0], "retained");
}

KR_TEST(MenuCancellationRetiresPendingAndVisibleRequests) {
  IntegrationHost host;
  host.ui.request_context_menu(host.ui.self, KR_MENU_PAGE, 0, {});
  const auto request = host.menu;
  host.ui.dismiss_menu(host.ui.self, request);
  host.ui.dismiss_menu(host.ui.self, request);
  KR_EXPECT_EQ(host.closed_menus, 1);
  const KrMenuItem item{
      .struct_size = sizeof(item), .type = KR_MENU_ITEM_COMMAND, .submenu = -1};
  const KrMenu menu{sizeof(menu), &item, 1, sizeof(item)};
  host.ui.show_menu(host.ui.self, request, &menu, 1, sizeof(menu));
  KR_EXPECT(!headless::LastMenu());
  host.ui.request_context_menu(host.ui.self, KR_MENU_PAGE, 0, {});
  host.ui.show_menu(host.ui.self, host.menu, &menu, 1, sizeof(menu));
  KR_ASSERT(headless::LastMenu().has_value());
  host.ui.dismiss_menu(host.ui.self, host.menu);
  KR_EXPECT(!headless::LastMenu());
  KR_EXPECT_EQ(host.closed_menus, 2);
}

KR_TEST(WorkspaceEditorCreatesOwnedTrimmedEdits) {
  IntegrationHost host;
  KR_EXPECT_EQ(host.ui.show_workspace_editor(host.ui.self, 0), KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().workspace_editor_open);
  KR_EXPECT(!headless::LastRender().workspace_can_remove);
  KR_EXPECT(!headless::Delegate()->OnWorkspaceSubmitted(
      WorkspaceEditKind::kUpsert, " \n ", {}));
  KR_EXPECT_EQ(host.workspace_edits, 0);
  KR_EXPECT(headless::Delegate()->OnWorkspaceSubmitted(
      WorkspaceEditKind::kUpsert, "  Personal  ", "P"));
  KR_EXPECT_EQ(host.workspace_name, "Personal");
  KR_EXPECT_EQ(host.workspace_glyph, "P");
  KR_EXPECT_EQ(host.workspace_id, KrSpaceId{0});
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().workspace_editor_open);
}

KR_TEST(WorkspaceEditorProtectsDefaultAndValidatesHostCapability) {
  IntegrationHost host;
  KR_EXPECT_EQ(host.ui.show_workspace_editor(host.ui.self, 99),
               KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT_EQ(host.ui.show_workspace_editor(host.ui.self, 1), KR_OK);
  KR_EXPECT(!headless::Delegate()->OnWorkspaceSubmitted(
      WorkspaceEditKind::kRemove, {}, {}));
  KR_EXPECT_EQ(host.ui.show_workspace_editor(host.ui.self, 2), KR_OK);
  KR_EXPECT(headless::Delegate()->OnWorkspaceSubmitted(
      WorkspaceEditKind::kRemove, {}, {}));
  KR_EXPECT_EQ(host.workspace_id, KrSpaceId{2});
  KR_EXPECT_EQ(host.edit_kind, std::uint32_t{KR_WORKSPACE_REMOVE});
}

KR_TEST(WorkspaceEditorRequiresOptionalHostSlot) {
  IntegrationHost host(false);
  KR_EXPECT_EQ(host.ui.show_workspace_editor(host.ui.self, 0),
               KR_ERR_UNSUPPORTED);
}

KR_TEST(WorkspaceEditorIsDismissedWhenItsWorkspaceDisappears) {
  IntegrationHost host;
  host.ui.show_workspace_editor(host.ui.self, 2);
  const KrSpace remaining{sizeof(remaining), 1, Borrow("Default"), {}, 0};
  host.ui.set_spaces(host.ui.self, &remaining, 1, sizeof(remaining), 1);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().workspace_editor_open);
  KR_EXPECT_EQ(host.workspace_edits, 0);
}

KR_TEST(ShellMetricsUseDipsAndSkipUnchangedWindowUpdates) {
  IntegrationHost host;
  KR_EXPECT(host.metrics.minimum_client_width > 0);
  KR_EXPECT(host.metrics.minimum_client_height > 0);
  const auto calls = host.metrics_calls;
  host.Window(600, 450, 2);
  host.Window(500, 400, 1.5f);
  KR_EXPECT_EQ(host.metrics_calls, calls);
}

KR_TEST(MetricsResizeCannotPublishStalePageBounds) {
  IntegrationHost host;
  host.content_rights.clear();
  host.resize_on_metrics = true;
  headless::Delegate()->OnNativeTitleBarChanged(
      {.enabled = true, .height = 40, .left_inset = 250, .right_inset = 250});
  headless::RunPendingTasks();
  KR_EXPECT(!host.content_rights.empty());
  for (const auto right : host.content_rights)
    KR_EXPECT(right > 1000);
}

KR_TEST(ResolvedBackdropIsCoalescedAndUsesPublicLayerData) {
  IntegrationHost host;
  KR_EXPECT(host.backdrop_calls > 0);
  const auto calls = host.backdrop_calls;
  const KrTheme theme{sizeof(theme), false, false, false, 0xFF668877};
  host.ui.set_theme(host.ui.self, &theme);
  headless::RunPendingTasks();
  const auto themed_calls = host.backdrop_calls;
  KR_EXPECT(host.backdrop.glance_button != 0);
  KR_EXPECT(host.backdrop.glance_button_hover == 0xFF292929u);
  KR_EXPECT(host.backdrop.glance_button_icon != 0);
  KR_EXPECT(host.backdrop.glance_shadow == 0x3D000000u);
  KR_EXPECT(host.backdrop.glance_button_shadow == 0x12000000u);
  host.ui.set_theme(host.ui.self, &theme);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.backdrop_calls, themed_calls);
  KR_EXPECT(host.backdrop_calls >= calls);
  KrWorkspaceTheme workspace{};
  workspace.struct_size = sizeof(workspace);
  workspace.space = 1;
  workspace.flags = KR_THEME_CUSTOMIZED;
  workspace.point_count = 3;
  workspace.opacity = 0.6f;
  workspace.texture = 0.5f;
  workspace.lightness = 50;
  workspace.points[0].color = 0xFF775544;
  workspace.points[1].color = 0xFF447755;
  workspace.points[2].color = 0xFF554477;
  KR_EXPECT_EQ(host.ui.set_workspace_theme(host.ui.self, &workspace), KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.backdrop.layer_count, std::size_t{3});
  KR_EXPECT_EQ(host.backdrop.layers[0].kind, std::uint32_t{KR_GRADIENT_RADIAL});
  KR_EXPECT(host.backdrop.grain.width > 0);
  KR_EXPECT_EQ(host.backdrop.texture, 0.5f);
}

KR_TEST(QualityGlassAsksTheHostForATransparentFrameOnlyInQuality) {
  IntegrationHost host;
  KR_EXPECT_EQ(host.backdrop.flags, 0u);
  const auto set_glass = [&](std::uint64_t mode) {
    KrAppearancePrefs prefs{};
    prefs.struct_size = sizeof(prefs);
    prefs.glass_mode = mode;
    KR_EXPECT_EQ(host.ui.set_appearance_prefs(host.ui.self, &prefs), KR_OK);
    headless::RunPendingTasks();
  };
  set_glass(KR_GLASS_PERFORMANCE);
  KR_EXPECT_EQ(host.backdrop.flags, 0u);
  const auto calls = host.backdrop_calls;
  set_glass(KR_GLASS_QUALITY);
  KR_EXPECT(host.backdrop_calls > calls);
  KR_EXPECT_EQ(host.backdrop.flags,
               std::uint32_t{KR_WINDOW_BACKDROP_TRANSPARENT});
  const KrTheme contrast{sizeof(contrast), false, false, true, 0xFF668877};
  host.ui.set_theme(host.ui.self, &contrast);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.backdrop.flags, 0u);
  const KrTheme normal{sizeof(normal), false, false, false, 0xFF668877};
  host.ui.set_theme(host.ui.self, &normal);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.backdrop.flags,
               std::uint32_t{KR_WINDOW_BACKDROP_TRANSPARENT});
  set_glass(KR_GLASS_OFF);
  KR_EXPECT_EQ(host.backdrop.flags, 0u);
}
}  // namespace
}  // namespace kr
