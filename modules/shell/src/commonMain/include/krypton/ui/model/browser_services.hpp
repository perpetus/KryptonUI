#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "krypton/ui/core/flags.hpp"
#include "krypton/ui/core/ids.hpp"
#include "krypton/ui/core/result.hpp"
namespace kr {
using SearchEngineId = Id<struct SearchEngineTag>;
enum class DefaultBrowserFlag : std::uint32_t {
  kIsDefault = 1u << 0,
  kCanSet = 1u << 1,
  kManaged = 1u << 2,
  kPending = 1u << 3
};
template <>
inline constexpr bool kIsFlagEnum<DefaultBrowserFlag> = true;

enum class SearchEngineFlag : std::uint32_t {
  kDefault = 1u << 0,
  kManaged = 1u << 1
};
template <>
inline constexpr bool kIsFlagEnum<SearchEngineFlag> = true;
struct SearchEngine {
  SearchEngineId id;
  Flags<SearchEngineFlag> flags;
  std::uint32_t order = 0;
  std::string name, keyword;
  ImageId icon;
};

// Host services outlive onboarding visibility and drafts.
struct BrowserServicesState {
  [[nodiscard]] Result<void> SetDefaultBrowser(Flags<DefaultBrowserFlag> flags);
  [[nodiscard]] Result<void> UpsertSearchEngine(SearchEngine value);
  [[nodiscard]] Result<void> RemoveSearchEngine(SearchEngineId id);
  [[nodiscard]] const SearchEngine* FindSearchEngine(
      SearchEngineId id) const noexcept;
  [[nodiscard]] SearchEngineId DefaultEngine() const noexcept;
  [[nodiscard]] bool SearchManaged() const noexcept;
  std::vector<SearchEngine> engines;
  Flags<DefaultBrowserFlag> default_browser;
  bool default_browser_known = false;
  std::uint64_t revision = 0;
};
}  // namespace kr
