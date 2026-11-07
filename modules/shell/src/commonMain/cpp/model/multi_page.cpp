#include "krypton/ui/model/multi_page.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kr {
namespace {
constexpr float kMinimumWeight = 0.07f;

bool FiniteRect(Rect rect) noexcept {
  return std::isfinite(rect.x) && std::isfinite(rect.y) &&
         std::isfinite(rect.width) && std::isfinite(rect.height) &&
         std::isfinite(rect.right()) && std::isfinite(rect.bottom()) &&
         rect.width >= 0 && rect.height >= 0;
}

// Three internal nodes encode every supported topology. Leaves use negative
// indices, so topology and traversal need no allocation or recursive objects.
struct Node {
  DividerAxis axis;
  int first;
  int second;
};
using Tree = std::array<Node, kMaxSplitDividers>;

Tree Topology(const SplitGroup& group) noexcept {
  const bool main = group.layout >= SplitLayout::kMainLeft;
  if (main) {
    const bool horizontal = group.layout == SplitLayout::kMainTop ||
                            group.layout == SplitLayout::kMainBottom;
    const bool trailing = group.layout == SplitLayout::kMainRight ||
                          group.layout == SplitLayout::kMainBottom;
    const auto axis = horizontal ? DividerAxis::kRows : DividerAxis::kColumns;
    const auto cross = horizontal ? DividerAxis::kColumns : DividerAxis::kRows;
    const int companions = group.count == 2 ? -2 : 1;
    return {Node{axis, trailing ? companions : -1, trailing ? -1 : companions},
            Node{cross, -2, group.count == 3 ? -3 : 2}, Node{cross, -3, -4}};
  }
  const auto axis = group.layout == SplitLayout::kRows ? DividerAxis::kRows
                                                       : DividerAxis::kColumns;
  if (group.count == 2)
    return {Node{axis, -1, -2}, Node{}, Node{}};
  if (group.layout == SplitLayout::kGrid) {
    if (group.count == 3)
      return {Node{axis, 1, -3}, Node{DividerAxis::kRows, -1, -2}, Node{}};
    return {Node{axis, 1, 2}, Node{DividerAxis::kRows, -1, -2},
            Node{DividerAxis::kRows, -3, -4}};
  }
  if (group.count == 3)
    return {Node{axis, -1, 1}, Node{axis, -2, -3}, Node{}};
  return {Node{axis, 1, 2}, Node{axis, -1, -2}, Node{axis, -3, -4}};
}

float ResetNodeWeights(const Tree& tree,
                       SplitGroup& group,
                       int node,
                       std::span<const float> shares) noexcept {
  if (node < 0)
    return shares[static_cast<std::size_t>(-node - 1)];
  const auto index = static_cast<std::size_t>(node);
  const float first = ResetNodeWeights(tree, group, tree[index].first, shares);
  const float second =
      ResetNodeWeights(tree, group, tree[index].second, shares);
  group.weights[index] = first / (first + second);
  return first + second;
}

void ResetWeights(SplitGroup& group) noexcept {
  group.weights.fill(0.5f);
  std::array<float, kMaxSplitMembers> shares{1, 1, 1, 1};
  if (group.layout == SplitLayout::kGrid ||
      group.layout >= SplitLayout::kMainLeft) {
    // The main page occupies half of the primary axis. Companions share
    // the other half, independently of their count on the cross axis.
    if (group.layout >= SplitLayout::kMainLeft)
      shares[0] = static_cast<float>(group.count - 1);
    else if (group.count == 3)
      shares[2] = 2;
  } else if (group.layout == SplitLayout::kWideLeft) {
    shares[0] = 2;
  } else if (group.layout == SplitLayout::kWideRight) {
    shares[group.count - 1] = 2;
  } else if (group.layout == SplitLayout::kWideCenter) {
    shares[group.count / 2] = 2;
  }
  ResetNodeWeights(Topology(group), group, 0,
                   std::span(shares).first(group.count));
}

void Place(const Tree& tree,
           const SplitGroup& group,
           int index,
           Rect bounds,
           float width,
           MultiPageGeometry& out) noexcept {
  if (index < 0) {
    out.panes[static_cast<std::size_t>(-index - 1)] = bounds;
    return;
  }
  const auto node_index = static_cast<std::size_t>(index);
  const auto& node = tree[node_index];
  const bool columns = node.axis == DividerAxis::kColumns;
  const float extent = columns ? bounds.width : bounds.height;
  const float gap = std::min(width, extent);
  const float first_size = (extent - gap) * group.weights[node_index];
  Rect first = bounds;
  Rect second = bounds;
  Rect divider = bounds;
  if (columns) {
    first.width = first_size;
    divider.x += first_size;
    divider.width = gap;
    second.x += first_size + gap;
    second.width = extent - first_size - gap;
  } else {
    first.height = first_size;
    divider.y += first_size;
    divider.height = gap;
    second.y += first_size + gap;
    second.height = extent - first_size - gap;
  }
  out.dividers[node_index] = {divider, bounds, node.axis,
                              static_cast<std::uint32_t>(node_index)};
  Place(tree, group, node.first, first, width, out);
  Place(tree, group, node.second, second, width, out);
}
}  // namespace

bool IsValidSplit(const SplitGroup& group) noexcept {
  if (!group.id || group.revision == 0 || group.count < 2 ||
      group.count > kMaxSplitMembers || !IsValidSplitLayout(group.layout))
    return false;
  bool active_found = false;
  for (std::size_t i = 0; i < group.count; ++i) {
    if (!group.members[i])
      return false;
    active_found |= group.members[i] == group.active;
    for (std::size_t j = 0; j < i; ++j) {
      if (group.members[i] == group.members[j])
        return false;
    }
  }
  for (const float weight : group.weights) {
    if (!std::isfinite(weight) || weight < kMinimumWeight ||
        weight > 1.0f - kMinimumWeight)
      return false;
  }
  return active_found;
}

MultiPageResult<SplitGroup> MakeSplit(SplitGroupId id,
                                      SpaceId space,
                                      std::span<const TabId> tabs,
                                      TabId active,
                                      SplitLayout layout) noexcept {
  if (tabs.size() < 2 || tabs.size() > kMaxSplitMembers)
    return std::unexpected(MultiPageError::kInvalidGroup);
  SplitGroup group{.id = id,
                   .space = space,
                   .layout = layout,
                   .count = tabs.size(),
                   .active = active};
  std::ranges::copy(tabs, group.members.begin());
  ResetWeights(group);
  if (!IsValidSplit(group))
    return std::unexpected(MultiPageError::kInvalidGroup);
  return group;
}

MultiPageResult<std::optional<SplitGroup>> ReduceSplit(
    const SplitGroup& group,
    const SplitEdit& edit) noexcept {
  if (!IsValidSplit(group))
    return std::unexpected(MultiPageError::kInvalidGroup);
  if (edit.revision != group.revision ||
      group.revision == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected(MultiPageError::kStaleRevision);
  auto next = group;
  const auto found = std::ranges::find(group.tabs(), edit.tab);
  const auto position = static_cast<std::size_t>(found - group.tabs().begin());
  switch (edit.kind) {
    case SplitEditKind::kActivate:
      if (position == group.count)
        return std::unexpected(MultiPageError::kInvalidTab);
      next.active = edit.tab;
      break;
    case SplitEditKind::kLayout:
      if (!IsValidSplitLayout(edit.layout))
        return std::unexpected(MultiPageError::kInvalidGroup);
      next.layout = edit.layout;
      ResetWeights(next);
      break;
    case SplitEditKind::kInsert:
      if (group.count == kMaxSplitMembers)
        return std::unexpected(MultiPageError::kFull);
      if (!edit.tab || edit.index > group.count)
        return std::unexpected(MultiPageError::kInvalidTab);
      if (position != group.count)
        return std::unexpected(MultiPageError::kDuplicateTab);
      if (edit.space != group.space)
        return std::unexpected(MultiPageError::kWrongWorkspace);
      std::move_backward(next.members.begin() + edit.index,
                         next.members.begin() + next.count,
                         next.members.begin() + next.count + 1);
      next.members[edit.index] = edit.tab;
      ++next.count;
      ResetWeights(next);
      break;
    case SplitEditKind::kDetach:
      if (position == group.count)
        return std::unexpected(MultiPageError::kInvalidTab);
      if (group.count == 2)
        return std::optional<SplitGroup>{};
      std::move(next.members.begin() + position + 1,
                next.members.begin() + next.count,
                next.members.begin() + position);
      next.members[--next.count] = {};
      if (next.active == edit.tab)
        next.active = next.members[std::min(position, next.count - 1)];
      ResetWeights(next);
      break;
    case SplitEditKind::kMove:
      if (position == group.count || edit.index >= group.count)
        return std::unexpected(MultiPageError::kInvalidTab);
      if (position < edit.index)
        std::rotate(next.members.begin() + position,
                    next.members.begin() + position + 1,
                    next.members.begin() + edit.index + 1);
      else
        std::rotate(next.members.begin() + edit.index,
                    next.members.begin() + position,
                    next.members.begin() + position + 1);
      break;
    case SplitEditKind::kResize:
      if (edit.index >= group.count - 1 || !std::isfinite(edit.weight))
        return std::unexpected(MultiPageError::kInvalidGeometry);
      next.weights[edit.index] =
          std::clamp(edit.weight, kMinimumWeight, 1.0f - kMinimumWeight);
      break;
    case SplitEditKind::kUnsplit:
      return std::optional<SplitGroup>{};
    case SplitEditKind::kWorkspace:
      if (!edit.space)
        return std::unexpected(MultiPageError::kWrongWorkspace);
      next.space = edit.space;
      break;
    default:
      return std::unexpected(MultiPageError::kInvalidTransition);
  }
  ++next.revision;
  return std::optional(next);
}

MultiPageResult<SplitCopyPlan> PlanSplitCopies(std::span<const Tab> selected,
                                               SpaceId space) noexcept {
  if (selected.size() < 2 || selected.size() > kMaxSplitMembers)
    return std::unexpected(MultiPageError::kInvalidGroup);
  bool any_pinned = false;
  bool all_pinned = true;
  bool any_essential = false;
  SplitCopyPlan plan{.count = selected.size()};
  for (std::size_t i = 0; i < selected.size(); ++i) {
    const auto& tab = selected[i];
    if (!tab.id)
      return std::unexpected(MultiPageError::kInvalidTab);
    if (tab.space != space && !tab.flags.Has(TabFlag::kFavorite))
      return std::unexpected(MultiPageError::kWrongWorkspace);
    for (std::size_t j = 0; j < i; ++j) {
      if (selected[j].id == tab.id)
        return std::unexpected(MultiPageError::kDuplicateTab);
    }
    const bool pinned = tab.flags.Has(TabFlag::kPinned);
    any_pinned |= pinned;
    all_pinned &= pinned;
    any_essential |= tab.flags.Has(TabFlag::kFavorite);
    plan.sources[i] = tab.id;
  }
  const bool regular_copies = any_essential || (any_pinned && !all_pinned);
  plan.pinned = all_pinned && !regular_copies;
  for (std::size_t i = 0; i < selected.size(); ++i)
    plan.copy[i] = selected[i].flags.Has(TabFlag::kFavorite) ||
                   (selected[i].flags.Has(TabFlag::kPinned) && !plan.pinned);
  return plan;
}

bool CanRestoreSplit(const SplitGroup& group,
                     std::span<const Tab> restored) noexcept {
  if (!IsValidSplit(group))
    return false;
  std::optional<bool> pinned;
  for (const auto id : group.tabs()) {
    const auto found = std::ranges::find(restored, id, &Tab::id);
    if (found == restored.end() || found->space != group.space ||
        found->flags.Has(TabFlag::kFavorite))
      return false;
    const bool member_pinned = found->flags.Has(TabFlag::kPinned);
    if (pinned && *pinned != member_pinned)
      return false;
    pinned = member_pinned;
  }
  return true;
}

MultiPageResult<MultiPageGeometry> LayoutSplit(const SplitGroup& group,
                                               Rect page,
                                               float divider_width,
                                               bool right_to_left) noexcept {
  if (!IsValidSplit(group))
    return std::unexpected(MultiPageError::kInvalidGroup);
  if (!FiniteRect(page) || !std::isfinite(divider_width) || divider_width < 0)
    return std::unexpected(MultiPageError::kInvalidGeometry);
  MultiPageGeometry result{.pane_count = group.count,
                           .divider_count = group.count - 1};
  Place(Topology(group), group, 0, page, divider_width, result);
  if (right_to_left) {
    const auto mirror = [page](Rect& rect) {
      rect.x =
          std::clamp(page.x + (page.width - (rect.x - page.x) - rect.width),
                     page.x, std::max(page.x, page.right() - rect.width));
    };
    for (std::size_t i = 0; i < result.pane_count; ++i)
      mirror(result.panes[i]);
    for (std::size_t i = 0; i < result.divider_count; ++i) {
      mirror(result.dividers[i].bounds);
      mirror(result.dividers[i].available);
    }
  }
  return result;
}

bool IsValidGlance(const GlanceState& state) noexcept {
  if (state.phase == GlancePhase::kClosed)
    return !state.identity.parent && !state.identity.child && !state.departure;
  if (state.phase != GlancePhase::kOpen && state.phase != GlancePhase::kClosing)
    return false;
  return state.identity.parent && state.identity.child &&
         state.identity.generation != 0 &&
         state.identity.parent != state.identity.child &&
         state.departure != state.identity.child &&
         (state.phase != GlancePhase::kOpen || !state.departure);
}

MultiPageResult<GlanceState> ReduceGlance(const GlanceState& state,
                                          const GlanceEdit& edit) noexcept {
  if (!IsValidGlance(state))
    return std::unexpected(MultiPageError::kInvalidTransition);
  if (edit.kind == GlanceEditKind::kOpen) {
    if (state.phase != GlancePhase::kClosed || !edit.identity.parent ||
        !edit.identity.child || edit.identity.parent == edit.identity.child ||
        edit.identity.generation <= state.identity.generation)
      return std::unexpected(MultiPageError::kInvalidTransition);
    return GlanceState{edit.identity, GlancePhase::kOpen, {}};
  }
  if (state.phase == GlancePhase::kClosed || state.identity != edit.identity)
    return std::unexpected(MultiPageError::kStaleRevision);
  auto next = state;
  switch (edit.kind) {
    case GlanceEditKind::kReuse:
      if (state.phase != GlancePhase::kOpen)
        return std::unexpected(MultiPageError::kInvalidTransition);
      return next;
    case GlanceEditKind::kRequestClose:
      if (state.phase != GlancePhase::kOpen ||
          edit.departure == state.identity.child)
        return std::unexpected(MultiPageError::kInvalidTransition);
      next.phase = GlancePhase::kClosing;
      next.departure = edit.departure;
      return next;
    case GlanceEditKind::kCancelClose:
      if (state.phase != GlancePhase::kClosing)
        return std::unexpected(MultiPageError::kInvalidTransition);
      next.phase = GlancePhase::kOpen;
      next.departure = {};
      return next;
    case GlanceEditKind::kClosed:
      if (state.phase != GlancePhase::kClosing)
        return std::unexpected(MultiPageError::kInvalidTransition);
      break;
    case GlanceEditKind::kSplit:
      if (edit.parent_members == 0 || edit.parent_members >= kMaxSplitMembers)
        return std::unexpected(MultiPageError::kFull);
      [[fallthrough]];
    case GlanceEditKind::kExpand:
      if (state.phase != GlancePhase::kOpen)
        return std::unexpected(MultiPageError::kInvalidTransition);
      break;
    default:
      return std::unexpected(MultiPageError::kInvalidTransition);
  }
  // Keep the generation watermark to reject delayed callbacks after closure.
  return GlanceState{
      {{}, {}, state.identity.generation}, GlancePhase::kClosed, {}};
}

TabId PresentedTab(TabId active, const GlanceState& glance) noexcept {
  return glance.phase != GlancePhase::kClosed && active == glance.identity.child
             ? glance.identity.parent
             : active;
}
MultiPageResult<GlanceGeometry> LayoutGlance(Rect parent,
                                             float inset,
                                             float toolbar_height) noexcept {
  if (!FiniteRect(parent) || !std::isfinite(inset) || inset < 0 ||
      !std::isfinite(toolbar_height) || toolbar_height < 0)
    return std::unexpected(MultiPageError::kInvalidGeometry);
  const float horizontal = std::min(inset, parent.width / 2);
  const float vertical = std::min(inset, parent.height / 2);
  const auto frame = parent.Inset(horizontal, vertical, horizontal, vertical);
  const float toolbar = std::min(toolbar_height, frame.height);
  return GlanceGeometry{
      frame,
      {frame.x, frame.y, frame.width, toolbar},
      {frame.x, frame.y + toolbar, frame.width, frame.height - toolbar}};
}

MultiPageResult<GlanceGeometry> LayoutGlance(Rect parent,
                                             const GlanceLayoutSpec& spec,
                                             bool controls_on_left) noexcept {
  if (!FiniteRect(parent) || !std::isfinite(spec.width_ratio) ||
      spec.width_ratio <= 0 || spec.width_ratio > 1 ||
      !std::isfinite(spec.controls_top) || spec.controls_top < 0 ||
      !std::isfinite(spec.controls_padding) || spec.controls_padding < 0 ||
      !std::isfinite(spec.button_size) || spec.button_size <= 0 ||
      !std::isfinite(spec.button_gap) || spec.button_gap < 0)
    return std::unexpected(MultiPageError::kInvalidGeometry);
  const float width = parent.width * spec.width_ratio;
  const Rect page{parent.x + (parent.width - width) / 2, parent.y, width,
                  parent.height};
  const float column_width = spec.button_size + 2 * spec.controls_padding;
  const float column_height =
      3 * spec.button_size + 2 * spec.button_gap + 2 * spec.controls_padding;
  if (!std::isfinite(column_width) || !std::isfinite(column_height))
    return std::unexpected(MultiPageError::kInvalidGeometry);
  const float cw = std::min(parent.width, column_width);
  const float ch = std::min(parent.height, column_height);
  const Rect controls{
      std::clamp(controls_on_left ? page.x - cw : page.right(), parent.x,
                 parent.right() - cw),
      parent.y + std::min(spec.controls_top, parent.height - ch), cw, ch};
  const float left = std::min(page.x, controls.x);
  const float top = std::min(page.y, controls.y);
  return GlanceGeometry{
      {left, top, std::max(page.right(), controls.right()) - left,
       std::max(page.bottom(), controls.bottom()) - top},
      controls,
      page};
}
Rect PaneHeaderBounds(Rect pane, float width, float height) noexcept {
  if (!FiniteRect(pane) || !std::isfinite(width) || !std::isfinite(height) ||
      width < 0 || height < 0)
    return {};
  width = std::min(width, pane.width);
  height = std::min(height, pane.height);
  return {pane.x + (pane.width - width) / 2, pane.y, width, height};
}
Rect PaneControlsBounds(Rect pane, float size, bool right_to_left) noexcept {
  if (!FiniteRect(pane) || !std::isfinite(size) || size < 0)
    return {};
  const float width = std::min(pane.width, size);
  const float height = std::min(pane.height, size);
  return {right_to_left ? pane.x : pane.right() - width, pane.y, width, height};
}
}  // namespace kr
