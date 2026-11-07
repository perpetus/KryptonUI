#include <cstddef>
#include <limits>

#include "krypton/testing/test.hpp"
#include "krypton/ui/platform/platform.hpp"
#include "krypton_ui/abi.h"

namespace kr {
KR_TEST(GlassAbiAppendsSlotsAndGuardsOptionalAppearanceTail) {
  KR_EXPECT_EQ(KR_UI_ABI_VERSION, 2u);
  KR_EXPECT(offsetof(KrAppearancePrefs, glass_mode) >
            offsetof(KrAppearancePrefs, icon_pack));
  KR_EXPECT(offsetof(KrShellUI, register_glass_backdrop) >
            offsetof(KrShellUI, remove_split_group));
  KrAppearancePrefs legacy{};
  legacy.struct_size = offsetof(KrAppearancePrefs, glass_mode);
  KR_EXPECT(!KR_HAS_FIELD(&legacy, KrAppearancePrefs, glass_mode));
  legacy.struct_size = sizeof(legacy);
  KR_EXPECT(KR_HAS_FIELD(&legacy, KrAppearancePrefs, glass_mode));
}
KR_TEST(GlassAbiRejectsMalformedNativeResourcesBeforePlatformDispatch) {
  // The bridge checks input independently of the platform and never
  // dereferences self for malformed resources. No native graphics is needed for
  // this test.
  KrBrowser browser{};
  browser.struct_size = sizeof(browser);
  KrShellUI ui{};
  ui.struct_size = sizeof(ui);
  KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr, &browser};
  KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
  KR_EXPECT(ui.register_glass_backdrop(ui.self, nullptr) ==
            KR_ERR_INVALID_ARGUMENT);
  KrGlassBackdropResource resource{};
  resource.struct_size = sizeof(resource);
  KR_EXPECT(ui.register_glass_backdrop(ui.self, &resource) ==
            KR_ERR_INVALID_ARGUMENT);
  resource = {sizeof(resource), 1, KR_GLASS_D3D11_BGRA8, 64, 64, 1, 2, 3, 1,
              {0, 0, 64, 64}};
  resource.bounds.width = std::numeric_limits<float>::quiet_NaN();
  KR_EXPECT(ui.register_glass_backdrop(ui.self, &resource) ==
            KR_ERR_INVALID_ARGUMENT);
  KrGlassBackdropFrame frame{sizeof(frame), 1, 1, 0, 0};
  KR_EXPECT(ui.submit_glass_backdrop(ui.self, &frame) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(ui.drop_glass_backdrop(ui.self, 0) == KR_ERR_INVALID_ARGUMENT);
  ui.destroy(ui.self);
}
}  // namespace kr
