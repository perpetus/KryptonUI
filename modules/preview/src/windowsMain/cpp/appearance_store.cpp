// clang-format off
#include <windows.h>
// clang-format on

#include "appearance_store.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "krypton/ui/design/tokens.generated.hpp"

namespace kr::preview {
namespace {

constexpr std::size_t kMaxFileBytes = 4096;
constexpr std::uint32_t kKnownLayoutFlags = (1u << 17u) - 1u;
constexpr float kMaxSidebarWidth = design::kTokens.sidebar.max_width;
constexpr std::string_view kSignature = "KryptonUIAppearance";
constexpr std::string_view kVersion = "5";

bool IsValid(const fixtures::AppearancePrefs& prefs) noexcept {
  for (const auto& t : prefs.workspace_themes) {
    if (!t.struct_size)
      continue;
    if (t.point_count > 3 || t.scheme > KR_THEME_DARK ||
        t.harmony > KR_THEME_TRIADIC || t.mode > KR_THEME_GRAYSCALE ||
        (t.flags & ~KR_THEME_CUSTOMIZED) ||
        !(t.opacity >= design::kTokens.theme.min_opacity &&
          t.opacity <= design::kTokens.theme.max_opacity) ||
        !(t.texture >= 0 && t.texture <= 1) ||
        !(t.lightness >= 0 && t.lightness <= 100))
      return false;
    for (std::size_t i = 0; i < t.point_count; ++i)
      if (!(t.points[i].x >= 0 && t.points[i].x <= 1 && t.points[i].y >= 0 &&
            t.points[i].y <= 1))
        return false;
  }
  const auto scheme = static_cast<std::uint32_t>(prefs.scheme);
  return scheme <= static_cast<std::uint32_t>(fixtures::ThemeScheme::kDark) &&
         prefs.menu_material <= KR_MENU_MATERIAL_ACRYLIC_THIN &&
         prefs.icon_pack <= KR_ICON_PACK_RADIX &&
         prefs.glass_mode <= KR_GLASS_QUALITY &&
         !(prefs.appearance_flags &
           ~(KR_APPEARANCE_HIDE_HOVER_IMAGES | KR_APPEARANCE_HIDE_HOVER_MEMORY |
             KR_APPEARANCE_ATTACHED_SEARCH_BAR |
             KR_APPEARANCE_SEARCH_BAR_FLOATING_ON_TYPE |
             KR_APPEARANCE_SEARCH_BAR_MENU_BACKDROP)) &&
         !(prefs.layout_flags & ~kKnownLayoutFlags) &&
         std::isfinite(prefs.sidebar_width) && prefs.sidebar_width >= 0.0f &&
         prefs.sidebar_width <= kMaxSidebarWidth;
}

class Fields {
 public:
  explicit Fields(std::string_view text) : text_(text) {}

  std::string_view Next() noexcept {
    const auto start = text_.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
      text_ = {};
      return {};
    }
    text_.remove_prefix(start);
    const auto end = text_.find_first_of(" \t\r\n");
    const auto field = text_.substr(0, end);
    text_.remove_prefix(field.size());
    return field;
  }

  template <typename T>
    requires(std::integral<T> || std::floating_point<T>)
  bool Number(T& value) noexcept {
    const auto field = Next();
    if (field.empty())
      return false;
    const auto [end, error] =
        std::from_chars(field.data(), field.data() + field.size(), value);
    return error == std::errc{} && end == field.data() + field.size();
  }

 private:
  std::string_view text_;
};

std::optional<fixtures::AppearancePrefs> Parse(std::string_view text) noexcept {
  Fields fields{text};
  fixtures::AppearancePrefs prefs;
  std::uint32_t scheme = 0;
  if (fields.Next() != kSignature)
    return std::nullopt;
  const auto version = fields.Next();
  // Version 4 appends the icon pack to version 3.
  const bool tail = version == "3" || version == "4" || version == kVersion;
  if ((version != "1" && version != "2" && !tail) ||
      fields.Next() != "scheme" || !fields.Number(scheme) ||
      fields.Next() != "layout_flags" || !fields.Number(prefs.layout_flags) ||
      fields.Next() != "sidebar_width" || !fields.Number(prefs.sidebar_width) ||
      fields.Next() != "workspace_colors") {
    return std::nullopt;
  }
  for (auto& color : prefs.workspace_colors) {
    if (!fields.Number(color))
      return std::nullopt;
  }
  if (tail && (fields.Next() != "menu_material" ||
               !fields.Number(prefs.menu_material) ||
               fields.Next() != "appearance_flags" ||
               !fields.Number(prefs.appearance_flags)))
    return std::nullopt;
  if (version == "2" || tail) {
    for (auto& t : prefs.workspace_themes) {
      if (fields.Next() != "workspace_theme" || !fields.Number(t.space) ||
          !fields.Number(t.flags) || !fields.Number(t.scheme) ||
          !fields.Number(t.harmony) || !fields.Number(t.mode) ||
          !fields.Number(t.point_count) || !fields.Number(t.opacity) ||
          !fields.Number(t.texture) || !fields.Number(t.lightness))
        return std::nullopt;
      t.struct_size = sizeof(t);
      for (auto& point : t.points)
        if (!fields.Number(point.x) || !fields.Number(point.y) ||
            !fields.Number(point.color))
          return std::nullopt;
    }
  }
  if ((version == "4" || version == kVersion) &&
      (fields.Next() != "icon_pack" || !fields.Number(prefs.icon_pack)))
    return std::nullopt;
  if (version == kVersion &&
      (fields.Next() != "glass_mode" || !fields.Number(prefs.glass_mode)))
    return std::nullopt;
  prefs.scheme = static_cast<fixtures::ThemeScheme>(scheme);
  if (!fields.Next().empty() || !IsValid(prefs))
    return std::nullopt;
  return prefs;
}

std::optional<fixtures::AppearancePrefs> Read(
    const std::filesystem::path& path) {
  std::ifstream file{path, std::ios::binary};
  if (!file)
    return std::nullopt;
  std::array<char, kMaxFileBytes> contents{};
  file.read(contents.data(), static_cast<std::streamsize>(contents.size()));
  const auto size = file.gcount();
  if (file.bad() || size == static_cast<std::streamsize>(contents.size()))
    return std::nullopt;
  return Parse({contents.data(), static_cast<std::size_t>(size)});
}

void Write(const std::filesystem::path& path,
           const fixtures::AppearancePrefs& prefs) {
  auto contents = std::format(
      "{} {}\nscheme {}\nlayout_flags {}\nsidebar_width {:.9g}\n"
      "workspace_colors {} {} {}\n",
      kSignature, kVersion, static_cast<std::uint32_t>(prefs.scheme),
      prefs.layout_flags, prefs.sidebar_width, prefs.workspace_colors[0],
      prefs.workspace_colors[1], prefs.workspace_colors[2]);
  std::format_to(std::back_inserter(contents),
                 "menu_material {}\nappearance_flags {}\n", prefs.menu_material,
                 prefs.appearance_flags);
  for (std::size_t i = 0; i < prefs.workspace_themes.size(); ++i) {
    auto t = prefs.workspace_themes[i];
    if (!t.struct_size) {
      t.opacity = design::kTokens.theme.opacity;
      t.lightness = 50;
    }
    std::format_to(std::back_inserter(contents),
                   "workspace_theme {} {} {} {} {} {} {:.9g} {:.9g} {:.9g}\n",
                   i + 1, t.flags, static_cast<std::uint32_t>(prefs.scheme),
                   t.harmony, t.mode, t.point_count, t.opacity, t.texture,
                   t.lightness);
    for (const auto& point : t.points)
      std::format_to(std::back_inserter(contents), "{:.9g} {:.9g} {}\n",
                     point.x, point.y, point.color);
  }
  std::format_to(std::back_inserter(contents), "icon_pack {}\n",
                 prefs.icon_pack);
  std::format_to(std::back_inserter(contents), "glass_mode {}\n",
                 prefs.glass_mode);

  std::error_code error;
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error) {
      OutputDebugStringW(L"KryptonUI: cannot create appearance directory.\n");
      return;
    }
  }
  auto temporary = path;
  temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId());
  {
    std::ofstream file{temporary, std::ios::binary | std::ios::trunc};
    if (!file) {
      OutputDebugStringW(L"KryptonUI: cannot write appearance preferences.\n");
      return;
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.flush();
    file.close();
    if (!file) {
      std::filesystem::remove(temporary, error);
      OutputDebugStringW(L"KryptonUI: appearance preferences write failed.\n");
      return;
    }
  }
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(temporary, error);
    OutputDebugStringW(
        L"KryptonUI: appearance preferences replacement failed.\n");
  }
}

}  // namespace

class AppearanceStore::Impl {
 public:
  Impl(std::filesystem::path path, Delivery loaded)
      : path_(std::move(path)), loaded_(std::move(loaded)) {
    // This worker cannot outlive Impl: its destructor joins before members go.
    worker_ = std::thread{[this] { Run(); }};
  }

  ~Impl() {
    {
      const std::lock_guard lock{mutex_};
      stopping_ = true;
    }
    changed_.notify_one();
    worker_.join();
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;

  void Save(fixtures::AppearancePrefs prefs) {
    if (!IsValid(prefs))
      return;
    {
      const std::lock_guard lock{mutex_};
      pending_ = prefs;
    }
    changed_.notify_one();
  }

  void FlushAsync(std::function<void()> completion) {
    if (!completion)
      return;
    {
      const std::lock_guard lock{mutex_};
      completions_.push_back(std::move(completion));
    }
    changed_.notify_one();
  }

 private:
  void Run() noexcept {
    std::optional<fixtures::AppearancePrefs> initial;
    try {
      initial = Read(path_);
    } catch (...) {
      OutputDebugStringW(L"KryptonUI: appearance preferences read failed.\n");
    }
    try {
      if (loaded_)
        loaded_(initial);
    } catch (...) {
      OutputDebugStringW(L"KryptonUI: appearance delivery failed.\n");
    }

    std::vector<std::function<void()>> completions;
    for (;;) {
      completions.clear();
      std::optional<fixtures::AppearancePrefs> pending;
      {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [this] {
          return stopping_ || pending_.has_value() || !completions_.empty();
        });
        pending = std::exchange(pending_, std::nullopt);
        completions.swap(completions_);
        if (!pending && completions.empty() && stopping_)
          return;
      }
      if (pending) {
        try {
          Write(path_, *pending);
        } catch (...) {
          OutputDebugStringW(L"KryptonUI: appearance persistence failed.\n");
        }
      }
      for (const auto& completion : completions) {
        try {
          completion();
        } catch (...) {
          OutputDebugStringW(L"KryptonUI: appearance flush delivery failed.\n");
        }
      }
    }
  }

  const std::filesystem::path path_;
  const Delivery loaded_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::optional<fixtures::AppearancePrefs> pending_;
  std::vector<std::function<void()>> completions_;
  bool stopping_ = false;
  std::thread worker_;
};

AppearanceStore::AppearanceStore(std::filesystem::path path, Delivery loaded)
    : impl_(std::make_unique<Impl>(std::move(path), std::move(loaded))) {}

AppearanceStore::~AppearanceStore() = default;

void AppearanceStore::Save(fixtures::AppearancePrefs prefs) {
  impl_->Save(prefs);
}

void AppearanceStore::FlushAsync(std::function<void()> completion) {
  impl_->FlushAsync(std::move(completion));
}

}  // namespace kr::preview
