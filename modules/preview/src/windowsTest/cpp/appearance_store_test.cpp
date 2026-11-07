// clang-format off
#include <windows.h>
// clang-format on

#include "appearance_store.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "krypton/testing/test.hpp"
#include "krypton/ui/design/tokens.generated.hpp"

namespace kr::preview {
namespace {

using fixtures::AppearancePrefs;
using fixtures::ThemeScheme;
using namespace std::chrono_literals;

class TestFile {
 public:
  TestFile() {
    static std::atomic<unsigned> next = 0;
    directory_ =
        std::filesystem::temp_directory_path() /
        std::format("krypton-appearance-test-{}-{}-{}", GetCurrentProcessId(),
                    GetTickCount64(), next.fetch_add(1));
    KR_ASSERT(std::filesystem::create_directory(directory_));
    path_ = directory_ / "appearance.txt";
  }

  ~TestFile() {
    // Remove only the two files this fixture can create, then its empty folder.
    // A failed test must never recursively remove an unexpected directory.
    std::error_code error;
    std::filesystem::remove(path_, error);
    auto temporary = path_;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId());
    std::filesystem::remove(temporary, error);
    std::filesystem::remove(directory_, error);
  }

  TestFile(const TestFile&) = delete;
  TestFile& operator=(const TestFile&) = delete;
  TestFile(TestFile&&) = delete;
  TestFile& operator=(TestFile&&) = delete;

  const std::filesystem::path& path() const { return path_; }

  void Write(std::string_view text) const {
    std::ofstream file{path_, std::ios::binary | std::ios::trunc};
    KR_ASSERT(file.good());
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    KR_ASSERT(file.good());
  }

  std::string Read() const {
    std::ifstream file{path_, std::ios::binary};
    KR_ASSERT(file.good());
    return {std::istreambuf_iterator<char>{file},
            std::istreambuf_iterator<char>{}};
  }

 private:
  std::filesystem::path directory_;
  std::filesystem::path path_;
};

struct Loaded {
  std::optional<AppearancePrefs> prefs;
  std::thread::id thread;
};

Loaded Load(const std::filesystem::path& path) {
  // Store destruction joins before promise destruction, including assertion
  // failure. The callback therefore cannot outlive this captured reference.
  std::promise<Loaded> completed;
  auto future = completed.get_future();
  AppearanceStore store{
      path, [&completed](std::optional<AppearancePrefs> prefs) {
        completed.set_value({prefs, std::this_thread::get_id()});
      }};
  KR_ASSERT(future.wait_for(2s) == std::future_status::ready);
  return future.get();
}

void ExpectPrefs(const AppearancePrefs& actual,
                 const AppearancePrefs& expected) {
  KR_EXPECT_EQ(actual.scheme, expected.scheme);
  KR_EXPECT_EQ(actual.layout_flags, expected.layout_flags);
  KR_EXPECT_EQ(actual.sidebar_width, expected.sidebar_width);
  KR_EXPECT_EQ(actual.menu_material, expected.menu_material);
  KR_EXPECT_EQ(actual.appearance_flags, expected.appearance_flags);
  KR_EXPECT_EQ(actual.icon_pack, expected.icon_pack);
  KR_EXPECT_EQ(actual.glass_mode, expected.glass_mode);
  KR_EXPECT_EQ(actual.workspace_colors, expected.workspace_colors);
}

KR_TEST(AppearanceStoreReportsMissingPreferencesFromItsWorker) {
  const TestFile file;
  const auto loaded = Load(file.path());
  KR_EXPECT(!loaded.prefs.has_value());
  KR_EXPECT(loaded.thread != std::this_thread::get_id());
  KR_EXPECT(!std::filesystem::exists(file.path()));
}

KR_TEST(AppearanceStoreKeepsTheLatestQueuedSnapshotAndFlushesOnShutdown) {
  const TestFile file;
  const AppearancePrefs first{
      .scheme = ThemeScheme::kLight,
      .layout_flags = KR_LAYOUT_TOOLBAR_SINGLE,
      .sidebar_width = 218.25f,
      .workspace_colors = {0u, 0xFF000000u, 0xFFFFFFFFu}};
  const AppearancePrefs last{
      .scheme = ThemeScheme::kDark,
      .layout_flags = KR_LAYOUT_SIDEBAR_RIGHT | KR_LAYOUT_TOOLBAR_COLLAPSED |
                      KR_LAYOUT_COMPACT_MODE | KR_LAYOUT_COMPACT_HIDE_SIDEBAR |
                      KR_LAYOUT_COMPACT_HIDE_TOOLBAR,
      .sidebar_width = 344.125f,
      .menu_material = KR_MENU_MATERIAL_ACRYLIC,
      .appearance_flags = KR_APPEARANCE_HIDE_HOVER_MEMORY,
      .workspace_colors = {0xFF5D566Au, 0u, 0x80447464u}};
  std::promise<void> loaded;
  auto load_finished = loaded.get_future();
  std::promise<void> release;
  auto may_write = release.get_future().share();
  std::atomic<bool> callback_waiting = false;
  {
    AppearanceStore store{file.path(), [&loaded, &callback_waiting, may_write](
                                           std::optional<AppearancePrefs>) {
                            callback_waiting.store(true);
                            loaded.set_value();
                            // Hold the worker in its callback so both UI-side
                            // Save calls replace the pending slot before it can
                            // begin writing. This also exercises that delivery
                            // never holds the Save mutex. The wait is bounded
                            // even when the test throws before releasing it.
                            static_cast<void>(may_write.wait_for(2s));
                            callback_waiting.store(false);
                          }};
    KR_ASSERT(load_finished.wait_for(2s) == std::future_status::ready);
    store.Save(first);
    store.Save(last);
    KR_EXPECT(callback_waiting.load());
    release.set_value();
    // No wait for a write: destruction must commit the final pending snapshot.
  }
  const auto restored = Load(file.path());
  KR_ASSERT(restored.prefs.has_value());
  ExpectPrefs(*restored.prefs, last);
  KR_EXPECT(restored.thread != std::this_thread::get_id());
}

KR_TEST(AppearanceStoreRoundTripsAllSchemesAndWidthBoundaries) {
  const TestFile file;
  const std::array widths{0.0f, design::kTokens.sidebar.min_width,
                          design::kTokens.sidebar.max_width};
  const std::array schemes{ThemeScheme::kSystem, ThemeScheme::kLight,
                           ThemeScheme::kDark};
  for (std::size_t index = 0; index < schemes.size(); ++index) {
    const AppearancePrefs expected{
        .scheme = schemes[index],
        .layout_flags = KR_LAYOUT_TABS_DYNAMIC | KR_LAYOUT_CENTERED_ADDRESS |
                        KR_LAYOUT_MINIMAL_ADDRESS | KR_LAYOUT_SQUARE_FRAME |
                        KR_LAYOUT_COMPACT_TARGETS_EXPLICIT,
        .sidebar_width = widths[index],
        .workspace_colors = {0u, 0xFFFFFFFFu, 0x01020304u}};
    {
      AppearanceStore store{file.path(), {}};
      store.Save(expected);
    }
    const auto restored = Load(file.path());
    KR_ASSERT(restored.prefs.has_value());
    ExpectPrefs(*restored.prefs, expected);
  }
}

KR_TEST(AppearanceStoreFlushCompletionCanReadTheSavedFileOnItsWorker) {
  const TestFile file;
  const AppearancePrefs expected{
      .scheme = ThemeScheme::kDark,
      .layout_flags = KR_LAYOUT_TOOLBAR_COLLAPSED,
      .sidebar_width = 312.625f,
      .workspace_colors = {0xFF5D566Au, 0u, 0xFFFFFFFFu}};
  struct Flushed {
    std::string contents;
    std::thread::id thread;
  };
  std::promise<Flushed> completed;
  auto future = completed.get_future();
  {
    AppearanceStore store{file.path(), {}};
    store.Save(expected);
    store.FlushAsync([&completed, path = file.path()] {
      std::ifstream saved{path, std::ios::binary};
      std::string contents{std::istreambuf_iterator<char>{saved},
                           std::istreambuf_iterator<char>{}};
      completed.set_value({std::move(contents), std::this_thread::get_id()});
    });
    KR_ASSERT(future.wait_for(2s) == std::future_status::ready);
    const auto flushed = future.get();
    KR_EXPECT(flushed.thread != std::this_thread::get_id());
    KR_EXPECT(flushed.contents.starts_with("KryptonUIAppearance 5\n"));
    KR_EXPECT(flushed.contents.contains("sidebar_width 312.625\n"));
    KR_EXPECT_EQ(flushed.contents, file.Read());
  }
  const auto restored = Load(file.path());
  KR_ASSERT(restored.prefs.has_value());
  ExpectPrefs(*restored.prefs, expected);
}

KR_TEST(AppearanceStoreRejectsMalformedVersionsUnknownFlagsAndOversizedFiles) {
  const TestFile file;
  const std::array<std::string, 15> invalid{
      "",
      "not an appearance file",
      "KryptonUIAppearance 2\nscheme 2\nlayout_flags 0\nsidebar_width 248\n"
      "workspace_colors 0 4294967295 0\n",
      "WrongSignature 1\nscheme 2\nlayout_flags 0\nsidebar_width 248\n"
      "workspace_colors 0 4294967295 0\n",
      "KryptonUIAppearance 1\nscheme 3\nlayout_flags 0\nsidebar_width 248\n"
      "workspace_colors 0 4294967295 0\n",
      "KryptonUIAppearance 1\nscheme 2\nlayout_flags 65536\nsidebar_width 248\n"
      "workspace_colors 0 4294967295 0\n",
      "KryptonUIAppearance 1\nscheme 2\nlayout_flags 0\nsidebar_width -1\n"
      "workspace_colors 0 4294967295 0\n",
      std::format("KryptonUIAppearance 1\nscheme 2\nlayout_flags 0\n"
                  "sidebar_width {}\nworkspace_colors 0 4294967295 0\n",
                  design::kTokens.sidebar.max_width + 1),
      "KryptonUIAppearance 1\nscheme 2\nlayout_flags 0\nsidebar_width nan\n"
      "workspace_colors 0 4294967295 0\n",
      "KryptonUIAppearance 1\nscheme 2\nlayout_flags 0\nsidebar_width inf\n"
      "workspace_colors 0 4294967295 0\n",
      "KryptonUIAppearance 1\nscheme 2\nlayout_flags 0\nsidebar_width 248\n"
      "workspace_colors 0 4294967296 0\n",
      "KryptonUIAppearance 1\nscheme 2\nlayout_flags 0\nsidebar_width 248\n"
      "workspace_colors 0 4294967295\n",
      "KryptonUIAppearance 1\nscheme 2\nlayout_flags 0\nsidebar_width 248\n"
      "workspace_colors 0 4294967295 0\nunexpected_field 1\n",
      "KryptonUIAppearance 1\nscheme 2\nlayout_flags 0\nsidebar_width 248px\n"
      "workspace_colors 0 4294967295 0\n",
      std::string(4096, 'x')};
  for (const auto& text : invalid) {
    file.Write(text);
    const auto restored = Load(file.path());
    KR_EXPECT(!restored.prefs.has_value());
    // Reading an invalid file must not rewrite or truncate it.
    KR_EXPECT_EQ(file.Read(), text);
  }
}

KR_TEST(AppearanceStoreIgnoresInvalidSavesWithoutReplacingValidPreferences) {
  const TestFile file;
  const AppearancePrefs valid{
      .scheme = ThemeScheme::kDark,
      .layout_flags = KR_LAYOUT_COMPACT_MODE | KR_LAYOUT_COMPACT_HIDE_TOOLBAR,
      .sidebar_width = 277.875f,
      .workspace_colors = {0xFF5D566Au, 0xFFFFFFFFu, 0u}};
  {
    AppearanceStore store{file.path(), {}};
    store.Save(valid);
  }
  const auto original = file.Read();
  {
    AppearanceStore store{file.path(), {}};
    for (const float width : {std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity(), -1.0f,
                              design::kTokens.sidebar.max_width + 1}) {
      auto invalid = valid;
      invalid.sidebar_width = width;
      store.Save(invalid);
    }
    auto invalid = valid;
    invalid.scheme = static_cast<ThemeScheme>(3u);
    store.Save(invalid);
    invalid = valid;
    invalid.layout_flags = 1u << 16u;
    store.Save(invalid);
  }
  KR_EXPECT_EQ(file.Read(), original);
  const auto restored = Load(file.path());
  KR_ASSERT(restored.prefs.has_value());
  ExpectPrefs(*restored.prefs, valid);
}

KR_TEST(AppearanceStoreRoundTripsGradientPointsAndMigratesVersionOne) {
  const TestFile file;
  file.Write(
      "KryptonUIAppearance 1\nscheme 2\nlayout_flags 0\nsidebar_width "
      "248\nworkspace_colors 4284307050 0 0\n");
  const auto legacy = Load(file.path());
  KR_ASSERT(legacy.prefs.has_value());
  KR_EXPECT_EQ(legacy.prefs->workspace_colors[0], 4284307050u);
  KR_EXPECT_EQ(legacy.prefs->workspace_themes[0].struct_size, 0u);
  auto expected = *legacy.prefs;
  auto& theme = expected.workspace_themes[1];
  theme.struct_size = sizeof(theme);
  theme.space = 2;
  theme.flags = KR_THEME_CUSTOMIZED;
  theme.scheme = KR_THEME_DARK;
  theme.harmony = KR_THEME_TRIADIC;
  theme.mode = KR_THEME_LIGHTNESS;
  theme.point_count = 3;
  theme.opacity = 0.625f;
  theme.texture = 0.375f;
  theme.lightness = 65;
  theme.points[0] = {0.625f, 0.375f, 0xFFCC2255};
  theme.points[1] = {0.25f, 0.625f, 0xFF339966};
  theme.points[2] = {0.75f, 0.5f, 0xFF4455CC};
  {
    AppearanceStore store{file.path(), {}};
    store.Save(expected);
  }
  const auto restored = Load(file.path());
  KR_ASSERT(restored.prefs.has_value());
  ExpectPrefs(*restored.prefs, expected);
  KR_EXPECT(file.Read().starts_with("KryptonUIAppearance 5\n"));
  auto version_two = file.Read();
  version_two.replace(0, std::string_view("KryptonUIAppearance 5").size(),
                      "KryptonUIAppearance 2");
  version_two.erase(version_two.find("icon_pack "));
  const auto extra_fields = version_two.find("menu_material ");
  const auto themes = version_two.find("workspace_theme ", extra_fields);
  KR_ASSERT(extra_fields != std::string::npos && themes != std::string::npos);
  version_two.erase(extra_fields, themes - extra_fields);
  file.Write(version_two);
  const auto migrated = Load(file.path());
  KR_ASSERT(migrated.prefs.has_value());
  ExpectPrefs(*migrated.prefs, expected);
  const auto& actual = restored.prefs->workspace_themes[1];
  KR_EXPECT_EQ(actual.space, theme.space);
  KR_EXPECT_EQ(actual.flags, theme.flags);
  KR_EXPECT_EQ(actual.scheme, theme.scheme);
  KR_EXPECT_EQ(actual.harmony, theme.harmony);
  KR_EXPECT_EQ(actual.mode, theme.mode);
  KR_EXPECT_EQ(actual.point_count, theme.point_count);
  KR_EXPECT_EQ(actual.opacity, theme.opacity);
  KR_EXPECT_EQ(actual.texture, theme.texture);
  KR_EXPECT_EQ(actual.lightness, theme.lightness);
  for (std::size_t i = 0; i < theme.point_count; ++i) {
    KR_EXPECT_EQ(actual.points[i].x, theme.points[i].x);
    KR_EXPECT_EQ(actual.points[i].y, theme.points[i].y);
    KR_EXPECT_EQ(actual.points[i].color, theme.points[i].color);
  }
}

}  // namespace
}  // namespace kr::preview
