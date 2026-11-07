#include "host_scene.hpp"

#include <algorithm>
#include <cmath>

#include "krypton/testing/test.hpp"
#include "krypton/ui/design/tokens.hpp"

namespace kr::preview {
KR_TEST(HostBackdropMatchesTheVisiblePageAtOffsetRegionsAndEveryPreviewDpi) {
  for (const float scale : {1.0f, 1.5f, 2.0f}) {
    HostScene scene;
    scene.client = {0, 0, static_cast<LONG>(640 * scale),
                    static_cast<LONG>(480 * scale)};
    scene.scale = scale;
    scene.dark = true;
    scene.has_browser = true;
    scene.gradient = {design::kTokens.dark.gradient_base,
                      design::kTokens.dark.text};
    scene.content = {20, 20, 600, 440};
    scene.radius = design::kTokens.radius.card;
    scene.layout.count = 1;
    scene.layout.surfaces[0].bounds = scene.content;
    scene.layout.surfaces[0].page.title = "Backdrop alignment";
    scene.layout.surfaces[0].page.url = "https://example.test";
    for (const bool checkerboard : {false, true}) {
      scene.checkerboard = checkerboard;
      scene.phase = 13;
      HostBackdropPainter full, region;
      const auto client_width = static_cast<UINT>(scene.client.right);
      const auto client_height = static_cast<UINT>(scene.client.bottom);
      const auto all =
          full.Paint(scene, {0, 0, 640, 480}, client_width, client_height);
      const auto width = static_cast<UINT>(200 * scale);
      const auto height = static_cast<UINT>(120 * scale);
      const auto cropped =
          region.Paint(scene, {60, 60, 200, 120}, width, height);
      KR_EXPECT(std::ranges::all_of(
          cropped, [](std::uint32_t pixel) { return (pixel >> 24) == 255; }));
      bool equal = true;
      for (UINT y = 0; y < height && equal; ++y)
        for (UINT x = 0; x < width; ++x)
          if (cropped[static_cast<std::size_t>(y) * width + x] !=
              all[(static_cast<std::size_t>(y) +
                   static_cast<UINT>(60 * scale)) *
                      client_width +
                  x + static_cast<UINT>(60 * scale)]) {
            equal = false;
            break;
          }
      KR_EXPECT(equal);
    }
  }
}
}  // namespace kr::preview
