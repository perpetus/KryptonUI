#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/model/model.hpp"

namespace kr {

inline constexpr std::size_t kMaxSplitMembers = 4;
inline constexpr std::size_t kMaxSplitDividers = kMaxSplitMembers - 1;

enum class SplitLayout : std::uint32_t {
  kColumns,
  kRows,
  kGrid,
  kWideLeft,
  kWideRight,
  kWideCenter,
  kMainLeft,
  kMainRight,
  kMainTop,
  kMainBottom,
};
[[nodiscard]] constexpr bool IsValidSplitLayout(SplitLayout layout) noexcept {
  return layout <= SplitLayout::kMainBottom;
}
[[nodiscard]] constexpr bool IsSnapSplitLayout(SplitLayout layout) noexcept {
  return layout > SplitLayout::kGrid && IsValidSplitLayout(layout);
}
enum class MultiPageError {
  kInvalidGroup,
  kInvalidTab,
  kDuplicateTab,
  kWrongWorkspace,
  kFull,
  kStaleRevision,
  kInvalidGeometry,
  kInvalidTransition,
};
template <typename T>
using MultiPageResult = std::expected<T, MultiPageError>;

// Value state is scoped to one host window. Revision changes on every accepted
// edit; delayed gestures must provide the revision they started against.
struct SplitGroup {
  SplitGroupId id;
  SpaceId space;
  std::uint64_t revision = 1;
  SplitLayout layout = SplitLayout::kColumns;
  std::size_t count = 0;
  std::array<TabId, kMaxSplitMembers> members{};
  TabId active;
  std::array<float, kMaxSplitDividers> weights{0.5f, 0.5f, 0.5f};

  [[nodiscard]] std::span<const TabId> tabs() const noexcept {
    return {members.data(), count <= members.size() ? count : 0};
  }
  friend bool operator==(const SplitGroup&, const SplitGroup&) = default;
};

enum class SplitEditKind {
  kActivate,
  kLayout,
  kInsert,
  kDetach,
  kMove,
  kResize,
  kUnsplit,
  kWorkspace
};
struct SplitEdit {
  SplitEditKind kind = SplitEditKind::kActivate;
  std::uint64_t revision = 0;
  TabId tab;
  SpaceId space;
  std::size_t index = 0;  // insertion/move position or divider index
  SplitLayout layout = SplitLayout::kColumns;
  float weight = 0.5f;
};

// A disengaged result dissolves the group; it never closes any member tabs.
[[nodiscard]] bool IsValidSplit(const SplitGroup& group) noexcept;
[[nodiscard]] MultiPageResult<std::optional<SplitGroup>> ReduceSplit(
    const SplitGroup& group,
    const SplitEdit& edit) noexcept;
[[nodiscard]] MultiPageResult<SplitGroup> MakeSplit(
    SplitGroupId id,
    SpaceId space,
    std::span<const TabId> tabs,
    TabId active,
    SplitLayout layout) noexcept;

struct SplitCopyPlan {
  std::size_t count = 0;
  std::array<TabId, kMaxSplitMembers> sources{};
  std::array<bool, kMaxSplitMembers> copy{};
  bool pinned = false;
};
[[nodiscard]] MultiPageResult<SplitCopyPlan> PlanSplitCopies(
    std::span<const Tab> selected,
    SpaceId space) noexcept;
// Recovery is all-or-nothing. The caller keeps all tabs when recovery fails.
[[nodiscard]] bool CanRestoreSplit(const SplitGroup& group,
                                   std::span<const Tab> restored) noexcept;

enum class DividerAxis : std::uint32_t { kColumns, kRows };
struct SplitDivider {
  Rect bounds;
  Rect available;  // parent extent used to normalize pointer coordinates
  DividerAxis axis = DividerAxis::kColumns;
  std::uint32_t index = 0;
};
struct MultiPageGeometry {
  std::size_t pane_count = 0;
  std::size_t divider_count = 0;
  std::array<Rect, kMaxSplitMembers> panes{};
  std::array<SplitDivider, kMaxSplitDividers> dividers{};
};
// Allocation-free, synchronous DIPs. No view, render scheduling or callbacks.
[[nodiscard]] MultiPageResult<MultiPageGeometry> LayoutSplit(
    const SplitGroup& group,
    Rect page,
    float divider_width,
    bool right_to_left = false) noexcept;

enum class GlancePhase : std::uint32_t { kClosed, kOpen, kClosing };
struct GlanceIdentity {
  TabId parent;
  TabId child;
  std::uint64_t generation = 0;
  friend bool operator==(const GlanceIdentity&,
                         const GlanceIdentity&) = default;
};
struct GlanceState {
  GlanceIdentity identity;
  GlancePhase phase = GlancePhase::kClosed;
  // Retained until Chromium confirms unload. A cancellation clears it.
  TabId departure;
  friend bool operator==(const GlanceState&, const GlanceState&) = default;
};
enum class GlanceEditKind {
  kOpen,
  kReuse,
  kRequestClose,
  kCancelClose,
  kClosed,
  kExpand,
  kSplit
};
struct GlanceEdit {
  GlanceEditKind kind = GlanceEditKind::kOpen;
  GlanceIdentity identity;
  TabId departure;
  std::size_t parent_members = 1;
};
[[nodiscard]] bool IsValidGlance(const GlanceState& state) noexcept;
[[nodiscard]] MultiPageResult<GlanceState> ReduceGlance(
    const GlanceState& state,
    const GlanceEdit& edit) noexcept;
[[nodiscard]] TabId PresentedTab(TabId active,
                                 const GlanceState& glance) noexcept;
struct GlanceGeometry {
  Rect frame;
  Rect toolbar;
  Rect page;
};
struct GlanceLayoutSpec {
  float width_ratio;
  float controls_top;
  float controls_padding;
  float button_size;
  float button_gap;
};
[[nodiscard]] MultiPageResult<GlanceGeometry>
LayoutGlance(Rect parent, float inset, float toolbar_height) noexcept;
[[nodiscard]] MultiPageResult<GlanceGeometry> LayoutGlance(
    Rect parent,
    const GlanceLayoutSpec& spec,
    bool controls_on_left) noexcept;
[[nodiscard]] Rect PaneHeaderBounds(Rect pane,
                                    float width,
                                    float height) noexcept;
[[nodiscard]] Rect PaneControlsBounds(Rect pane,
                                      float size,
                                      bool right_to_left) noexcept;

}  // namespace kr
