#include <stdexcept>

#include "krypton/fixtures/mock_browser.hpp"
#include "krypton/testing/test.hpp"

namespace kr::fixtures {

KR_TEST(MockBrowserForwardsBorrowedUnhandledKeysAndReportsConsumption) {
  const int message = 42;
  const void* received = nullptr;
  MockBrowser browser({}, {.unhandled_key = [&](const void* value) {
                        received = value;
                        return value == &message;
                      }});
  const auto& port = browser.table();
  KR_ASSERT(KR_HAS_SLOT(&port, KrBrowser, unhandled_key));
  KR_ASSERT(port.unhandled_key);
  KR_EXPECT(!port.unhandled_key(port.self, nullptr));
  KR_EXPECT(received == nullptr);
  KR_EXPECT(port.unhandled_key(port.self, &message));
  KR_EXPECT(received == &message);
}

KR_TEST(MockBrowserUnhandledKeyIsSafeWithoutACallbackOrOnHostFailure) {
  const int message = 42;
  MockBrowser empty({}, {});
  KR_EXPECT(!empty.table().unhandled_key(empty.table().self, &message));
  MockBrowser failing({}, {.unhandled_key = [](const void*) -> bool {
                        throw std::runtime_error("host shortcut failure");
                      }});
  KR_EXPECT(!failing.table().unhandled_key(failing.table().self, &message));
}

KR_TEST(MockBrowserValidatesLayoutSizeBeforeApplyingUserPreferences) {
  int changes = 0;
  MockBrowser browser(
      {}, {.appearance_changed = [&](const AppearancePrefs&) { ++changes; }});
  changes = 0;
  const auto& port = browser.table();
  const auto initial = browser.appearance();
  const KrLayoutPrefs incomplete{
      .struct_size = offsetof(KrLayoutPrefs, sidebar_width),
      .flags = KR_LAYOUT_TABS_CLASSIC,
  };
  port.set_layout_prefs(port.self, nullptr);
  port.set_layout_prefs(port.self, &incomplete);
  KR_EXPECT_EQ(browser.appearance().layout_flags, initial.layout_flags);
  KR_EXPECT_EQ(changes, 0);
  struct ExtendedLayout {
    KrLayoutPrefs record;
    std::uint64_t future;
  };
  const ExtendedLayout extended{
      {sizeof(ExtendedLayout), KR_LAYOUT_TABS_DYNAMIC, 350}, 42};
  port.set_layout_prefs(port.self, &extended.record);
  KR_EXPECT_EQ(browser.appearance().layout_flags,
               static_cast<std::uint32_t>(KR_LAYOUT_TABS_DYNAMIC));
  KR_EXPECT_EQ(browser.appearance().sidebar_width, 350.0f);
  KR_EXPECT_EQ(changes, 1);
}

}  // namespace kr::fixtures
