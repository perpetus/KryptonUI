#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {

KR_TEST(AbiTranslatesTextWithoutRetainingHostPointers) {
  headless::Reset();
  KrBrowser browser{};
  browser.struct_size = sizeof(browser);
  const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                              &browser};
  KrShellUI ui{};
  ui.struct_size = sizeof(ui);
  KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
  struct Cleanup {
    KrShellUI& ui;
    ~Cleanup() { ui.destroy(ui.self); }
  } cleanup{ui};
  KR_ASSERT(KR_HAS_SLOT(&ui, KrShellUI, set_ui_text));
  std::string text = "Neuen Tab im aktuellen Arbeitsbereich öffnen";
  const KrStr key{"new_tab", 7};
  KR_EXPECT_EQ(ui.set_ui_text(ui.self, key, {text.data(), text.size()}), KR_OK);
  ui.set_text_direction(ui.self, true);
  text.clear();
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::RenderCount(), 1u);
  KR_EXPECT_EQ(
      headless::LastRender().text[static_cast<std::size_t>(UiText::kNewTab)],
      std::string("Neuen Tab im aktuellen Arbeitsbereich öffnen"));
  KR_EXPECT(headless::LastRender().right_to_left);
  KR_EXPECT_EQ(ui.set_ui_text(ui.self, {"unknown", 7}, {"x", 1}),
               KR_ERR_INVALID_ARGUMENT);
}

KR_TEST(AbiAppendedSlotsDoNotOverwriteLegacyOutputBuffers) {
  headless::Reset();
  constexpr auto legacy_size = offsetof(KrShellUI, set_ui_text);
  KrShellUI ui{};
  const auto storage = std::as_writable_bytes(std::span{&ui, std::size_t{1}});
  for (std::size_t index = legacy_size; index < storage.size(); ++index)
    storage[index] = std::byte{0x5a};
  KrBrowser browser{};
  browser.struct_size = sizeof(browser);
  const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                              &browser};
  KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
  KR_EXPECT_EQ(ui.struct_size, legacy_size);
  for (std::size_t index = legacy_size; index < storage.size(); ++index)
    KR_EXPECT(storage[index] == std::byte{0x5a});
  ui.destroy(ui.self);
  headless::RunPendingTasks();
}

}  // namespace
}  // namespace kr
