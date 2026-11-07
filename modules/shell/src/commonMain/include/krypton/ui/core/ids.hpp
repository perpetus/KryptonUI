#pragma once

#include <compare>
#include <cstdint>
#include <format>
#include <functional>

namespace kr {

// Distinct id types, so a TabId can't be passed where a SpaceId is expected.
// Zero means "none" for every id type, matching abi.h.
template <typename Tag>
struct Id {
  std::uint64_t value = 0;

  [[nodiscard]] constexpr explicit operator bool() const { return value != 0; }
  friend constexpr auto operator<=>(const Id&, const Id&) = default;
};

using TabId = Id<struct TabTag>;
using FolderId = Id<struct FolderTag>;
using SplitGroupId = Id<struct SplitGroupTag>;
using SpaceId = Id<struct SpaceTag>;
using ActionId = Id<struct ActionTag>;
using ImageId = Id<struct ImageTag>;

}  // namespace kr

template <typename Tag>
struct std::hash<kr::Id<Tag>> {
  std::size_t operator()(kr::Id<Tag> id) const noexcept {
    return std::hash<std::uint64_t>{}(id.value);
  }
};

template <typename Tag>
struct std::formatter<kr::Id<Tag>> : std::formatter<std::uint64_t> {
  auto format(kr::Id<Tag> id, std::format_context& ctx) const {
    return std::formatter<std::uint64_t>::format(id.value, ctx);
  }
};
