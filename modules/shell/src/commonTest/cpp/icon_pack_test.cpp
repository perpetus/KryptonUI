#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {
struct LegacyAppearancePrefs {
  std::size_t struct_size;
  std::uint32_t color_scheme, menu_material, flags;
};

struct Host {
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};
  std::vector<std::uint64_t> requested;

  explicit Host(bool supports_edits = true) {
    headless::Reset();
    KrBrowser browser{.struct_size = sizeof(KrBrowser), .self = this};
    if (supports_edits)
      browser.set_appearance_prefs = [](void* self,
                                        const KrAppearancePrefs* prefs) {
        static_cast<Host*>(self)->requested.push_back(
            KR_HAS_FIELD(prefs, KrAppearancePrefs, icon_pack)
                ? prefs->icon_pack
                : ~std::uint64_t{0});
      };
    browser.set_layout_prefs = [](void*, const KrLayoutPrefs*) {};
    KrCreateParams params{sizeof(KrCreateParams), KR_UI_ABI_VERSION, nullptr,
                          &browser};
    KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
  }
  ~Host() { ui.destroy(ui.self); }

  KrResult Publish(std::size_t size, std::uint64_t pack) {
    const KrAppearancePrefs prefs{size, KR_COLOR_DARK, KR_MENU_MATERIAL_DEFAULT,
                                  0, pack};
    return ui.set_appearance_prefs(ui.self, &prefs);
  }

  KrResult PublishLegacy() {
    // An old host advertises sizeof its original structure, including the
    // nonzero tail padding that must never be mistaken for an icon pack.
    LegacyAppearancePrefs legacy;
    std::memset(&legacy, 0xA5, sizeof(legacy));
    legacy.struct_size = sizeof(legacy);
    legacy.color_scheme = KR_COLOR_DARK;
    legacy.menu_material = KR_MENU_MATERIAL_DEFAULT;
    legacy.flags = 0;
    KrAppearancePrefs prefs{};
    std::memcpy(&prefs, &legacy, sizeof(legacy));
    return ui.set_appearance_prefs(ui.self, &prefs);
  }

  void OpenAppearance() {
    KR_ASSERT(ui.show_settings(ui.self, KR_SETTINGS_APPEARANCE) == KR_OK);
    headless::RunPendingTasks();
  }
};
}  // namespace

KR_TEST(IconPacksDefaultToFluentAndFollowHostConfirmation) {
  Host host;
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().icon_pack == IconPack::kFluent);
  KR_ASSERT(host.Publish(sizeof(KrAppearancePrefs), KR_ICON_PACK_TABLER) ==
            KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().icon_pack == IconPack::kTabler);
  KR_ASSERT(host.PublishLegacy() == KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().icon_pack == IconPack::kFluent);
  KR_EXPECT(host.Publish(sizeof(KrAppearancePrefs), 9) ==
            KR_ERR_INVALID_ARGUMENT);
}

KR_TEST(IconPackSettingAsksTheHostWithoutChangingLocally) {
  Host host;
  KR_ASSERT(host.Publish(sizeof(KrAppearancePrefs), KR_ICON_PACK_FLUENT) ==
            KR_OK);
  host.OpenAppearance();
  KR_EXPECT(headless::LastRender().settings.state.icon_pack_editable);
  headless::Delegate()->OnAppearanceEdited(
      {AppearanceOption::kIconPack, KR_ICON_PACK_PHOSPHOR});
  headless::Delegate()->OnAppearanceEdited({AppearanceOption::kIconPack, 5});
  KR_ASSERT(host.requested.size() == 1);
  KR_EXPECT_EQ(host.requested[0],
               static_cast<std::uint64_t>(KR_ICON_PACK_PHOSPHOR));
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().icon_pack == IconPack::kFluent);
}

KR_TEST(IconPackEditingRequiresACompleteHostSnapshot) {
  Host host;
  host.OpenAppearance();
  KR_EXPECT(!headless::LastRender().settings.state.icon_pack_editable);
  const auto edit = [] {
    headless::Delegate()->OnAppearanceEdited(
        {AppearanceOption::kIconPack, KR_ICON_PACK_LUCIDE});
  };
  edit();
  KR_EXPECT(host.requested.empty());

  KR_ASSERT(host.PublishLegacy() == KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().settings.state.appearance_editable);
  KR_EXPECT(!headless::LastRender().settings.state.icon_pack_editable);
  edit();
  KR_EXPECT(host.requested.empty());

  KR_ASSERT(host.Publish(sizeof(KrAppearancePrefs), KR_ICON_PACK_TABLER) ==
            KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().settings.state.icon_pack_editable);
  edit();
  KR_EXPECT_EQ(host.requested.size(), 1u);

  // Receiving an older snapshot again withdraws the extension capability.
  KR_ASSERT(host.PublishLegacy() == KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().settings.state.icon_pack_editable);
  edit();
  KR_EXPECT_EQ(host.requested.size(), 1u);
}

KR_TEST(TruncatedIconPackFieldsRemainUnavailable) {
  Host host;
  KR_ASSERT(host.Publish(sizeof(KrAppearancePrefs), KR_ICON_PACK_RADIX) ==
            KR_OK);
  host.OpenAppearance();
  KR_EXPECT(headless::LastRender().settings.state.icon_pack_editable);
  KR_ASSERT(host.Publish(offsetof(KrAppearancePrefs, icon_pack) +
                             sizeof(KrAppearancePrefs::icon_pack) - 1,
                         KR_ICON_PACK_RADIX) == KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().icon_pack == IconPack::kFluent);
  KR_EXPECT(!headless::LastRender().settings.state.icon_pack_editable);
  headless::Delegate()->OnAppearanceEdited(
      {AppearanceOption::kIconPack, KR_ICON_PACK_PHOSPHOR});
  KR_EXPECT(host.requested.empty());
}

KR_TEST(IconPackEditingRequiresTheHostCallback) {
  Host host(false);
  KR_ASSERT(host.Publish(sizeof(KrAppearancePrefs), KR_ICON_PACK_LUCIDE) ==
            KR_OK);
  host.OpenAppearance();
  KR_EXPECT(headless::LastRender().icon_pack == IconPack::kLucide);
  KR_EXPECT(!headless::LastRender().settings.state.icon_pack_editable);
  headless::Delegate()->OnAppearanceEdited(
      {AppearanceOption::kIconPack, KR_ICON_PACK_PHOSPHOR});
  KR_EXPECT(host.requested.empty());
}

KR_TEST(IconPackValuesAreValidatedBeforeNarrowing) {
  Host host;
  const auto large = (std::uint64_t{1} << 32) | KR_ICON_PACK_TABLER;
  KR_EXPECT(host.Publish(sizeof(KrAppearancePrefs), large) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(host.Publish(sizeof(KrAppearancePrefs), ~std::uint64_t{0}) ==
            KR_ERR_INVALID_ARGUMENT);
  host.OpenAppearance();
  KR_EXPECT(headless::LastRender().icon_pack == IconPack::kFluent);
  KR_EXPECT(!headless::LastRender().settings.state.icon_pack_editable);
}

KR_TEST(IncognitoReachesTheThemeViewModel) {
  Host host;
  const KrTheme theme{sizeof(KrTheme), true, true, false, 0xFF7C5CFF};
  host.ui.set_theme(host.ui.self, &theme);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().incognito);
}
}  // namespace kr
