#include <algorithm>
#include <cmath>

#include "krypton/ui/state/store.hpp"

namespace kr {
Result<void> Store::OpenSplitChooser() {
  const auto active = presented_tab();
  if (!FindTab(active))
    return Fail(ErrorCode::kNotFound, "A split requires an active tab");
  auto& chooser = ui_.split_chooser;
  chooser = {};
  chooser.open = true;
  if (const auto* group = SplitForTab(active)) {
    chooser.selected = group->members;
    chooser.count = group->count;
    chooser.layout = group->layout;
  } else {
    chooser.selected[0] = active;
    chooser.count = 1;
  }
  MarkDirty(Dirty::kMultiPage);
  return {};
}
Result<void> Store::SetSplitSelection(std::span<const TabId> selected) {
  if (!ui_.split_chooser.open || selected.empty() ||
      selected.size() > kMaxSplitMembers)
    return Fail(ErrorCode::kInvalidArgument, "Choose one to four tabs");
  for (std::size_t i = 0; i < selected.size(); ++i) {
    const auto* tab = FindTab(selected[i]);
    if (!tab || IsTransientTab(selected[i]) ||
        (active_space_ && tab->space != active_space_ &&
         !tab->flags.Has(TabFlag::kFavorite)))
      return Fail(ErrorCode::kInvalidArgument,
                  "Tab is unavailable for this split");
    if (std::find(selected.begin(), selected.begin() + i, selected[i]) !=
        selected.begin() + i)
      return Fail(ErrorCode::kInvalidArgument, "Duplicate split selection");
  }
  ui_.split_chooser.selected.fill({});
  std::ranges::copy(selected, ui_.split_chooser.selected.begin());
  ui_.split_chooser.count = selected.size();
  MarkDirty(Dirty::kMultiPage);
  return {};
}

Result<void> Store::OpenSplitOptions(TabId tab, Rect anchor) {
  const auto* group = SplitForTab(tab);
  if (!group || !FindTab(tab))
    return Fail(ErrorCode::kNotFound, "Split options require a current pane");
  if (!std::isfinite(anchor.x) || !std::isfinite(anchor.y) ||
      !std::isfinite(anchor.width) || !std::isfinite(anchor.height) ||
      anchor.empty())
    return Fail(ErrorCode::kInvalidArgument, "Invalid split menu anchor");
  ui_.split_chooser = {.open = true,
                       .count = group->count,
                       .selected = group->members,
                       .layout = group->layout,
                       .layout_only = true,
                       .target = tab,
                       .anchor = anchor,
                       .group = group->id,
                       .group_revision = group->revision};
  MarkDirty(Dirty::kMultiPage);
  return {};
}
void Store::SetSplitChooserLayout(SplitLayout layout) {
  if (!IsValidSplitLayout(layout))
    return;
  if (ui_.split_chooser.open && ui_.split_chooser.layout != layout) {
    ui_.split_chooser.layout = layout;
    MarkDirty(Dirty::kMultiPage);
  }
}
void Store::CloseSplitChooser() {
  if (ui_.split_chooser.open) {
    ui_.split_chooser.open = false;
    MarkDirty(Dirty::kMultiPage);
  }
}
namespace {
Result<void> InvalidMultiPage() {
  return Fail(ErrorCode::kInvalidArgument, "Invalid or stale multi-page state");
}
}  // namespace

const SplitGroup* Store::SplitForTab(TabId tab) const noexcept {
  if (!multi_page_ || multi_page_->membership.empty())
    return nullptr;
  const auto member = multi_page_->membership.find(tab);
  if (member == multi_page_->membership.end())
    return nullptr;
  const auto group = multi_page_->groups.find(member->second);
  return group == multi_page_->groups.end() ? nullptr : &group->second;
}

Result<void> Store::UpsertSplitGroup(const SplitGroup& group) {
  if (!IsValidSplit(group))
    return InvalidMultiPage();
  std::optional<bool> pinned;
  for (const auto id : group.tabs()) {
    const auto* tab = FindTab(id);
    const auto* existing = SplitForTab(id);
    if (!tab || tab->space != group.space ||
        tab->flags.Has(TabFlag::kFavorite) || IsTransientTab(id) ||
        (existing && existing->id != group.id))
      return InvalidMultiPage();
    const bool member_pinned = tab->flags.Has(TabFlag::kPinned);
    if (pinned && *pinned != member_pinned)
      return InvalidMultiPage();
    pinned = member_pinned;
  }
  if (!multi_page_)
    multi_page_ = std::make_unique<MultiPageState>();
  const auto found = multi_page_->groups.find(group.id);
  bool structural = true;
  if (found != multi_page_->groups.end()) {
    const auto& previous = found->second;
    if (previous == group)
      return {};
    if (group.revision <= previous.revision)
      return InvalidMultiPage();
    structural = group.count != previous.count ||
                 group.members != previous.members ||
                 group.space != previous.space;
    if (!structural) {
      const bool focus_changed = previous.active != group.active;
      found->second = group;
      MarkDirty(Dirty::kMultiPage);
      if (focus_changed)
        MarkDirty(Dirty::kActiveTab);
      return {};
    }
  }
  const bool existed = found != multi_page_->groups.end();
  // Stage nodes before touching accepted state. reserve() alone reserves
  // buckets, not nodes; merge() transfers these prepared nodes without churn.
  std::unordered_map<TabId, SplitGroupId> added_members;
  std::unordered_map<SplitGroupId, SplitGroup> added_group;
  for (const auto id : group.tabs()) {
    if (!multi_page_->membership.contains(id))
      added_members.emplace(id, group.id);
  }
  if (!existed)
    added_group.emplace(group.id, group);
  if (!added_group.empty())
    multi_page_->groups.reserve(multi_page_->groups.size() +
                                added_group.size());
  if (!added_members.empty())
    multi_page_->membership.reserve(multi_page_->membership.size() +
                                    added_members.size());
  if (existed) {
    const auto previous = multi_page_->groups.find(group.id)->second;
    for (const auto id : previous.tabs()) {
      if (std::ranges::find(group.tabs(), id) == group.tabs().end())
        multi_page_->membership.erase(id);
    }
  }
  multi_page_->membership.merge(added_members);
  multi_page_->groups.merge(added_group);
  if (existed)
    multi_page_->groups.find(group.id)->second = group;
  MarkDirty(Dirty::kMultiPage | Dirty::kActiveTab);
  if (structural)
    MarkDirty(Dirty::kTabs);
  return {};
}

Result<void> Store::RemoveSplitGroup(SplitGroupId id, std::uint64_t revision) {
  if (!id || revision == 0)
    return InvalidMultiPage();
  if (!multi_page_)
    return {};
  const auto found = multi_page_->groups.find(id);
  if (found == multi_page_->groups.end())
    return {};
  if (revision < found->second.revision)
    return InvalidMultiPage();
  for (const auto tab : found->second.tabs())
    multi_page_->membership.erase(tab);
  multi_page_->groups.erase(found);
  if (ui_.split_chooser.layout_only && ui_.split_chooser.group == id)
    CloseSplitChooser();
  MarkDirty(Dirty::kTabs | Dirty::kMultiPage);
  return {};
}

Result<void> Store::SetGlance(const GlanceState& state) {
  if (!IsValidGlance(state))
    return InvalidMultiPage();
  if (state.phase == GlancePhase::kClosed) {
    if (state.identity.parent || state.identity.child || state.departure)
      return InvalidMultiPage();
  } else if (!state.identity.parent || !state.identity.child ||
             state.identity.parent == state.identity.child ||
             state.identity.generation == 0 ||
             !FindTab(state.identity.parent) ||
             !FindTab(state.identity.child) ||
             SplitForTab(state.identity.child) ||
             state.departure == state.identity.child ||
             (state.phase == GlancePhase::kOpen && state.departure)) {
    return InvalidMultiPage();
  }
  if (!multi_page_) {
    if (state.phase == GlancePhase::kClosed)
      return {};
    multi_page_ = std::make_unique<MultiPageState>();
  }
  const auto& previous = multi_page_->glance;
  if (state == previous)
    return {};
  if (state.identity.generation < previous.identity.generation ||
      (previous.phase != GlancePhase::kClosed &&
       state.phase == GlancePhase::kClosed &&
       state.identity.generation != previous.identity.generation) ||
      (previous.phase != GlancePhase::kClosed &&
       state.phase != GlancePhase::kClosed &&
       state.identity != previous.identity) ||
      (previous.phase == GlancePhase::kClosed &&
       state.phase != GlancePhase::kClosed &&
       state.identity.generation <= previous.identity.generation))
    return InvalidMultiPage();
  const bool membership_changed =
      previous.identity.child != state.identity.child;
  multi_page_->glance = state;
  MarkDirty(Dirty::kMultiPage);
  if (membership_changed)
    MarkDirty(Dirty::kActiveTab);
  if (membership_changed)
    MarkDirty(Dirty::kTabs);
  return {};
}

void Store::ForgetMultiPageTab(TabId id) {
  if (!multi_page_)
    return;
  if (const auto* found = SplitForTab(id)) {
    const auto old = *found;
    const auto next = ReduceSplit(
        old,
        {.kind = SplitEditKind::kDetach, .revision = old.revision, .tab = id});
    if (next && *next) {
      // Called before tab removal; the remaining tabs still exist.
      const auto updated = UpsertSplitGroup(**next);
      (void)updated;
    } else {
      const auto removed = RemoveSplitGroup(old.id, old.revision);
      (void)removed;
    }
  }
  auto& glance = multi_page_->glance;
  if (glance.phase != GlancePhase::kClosed &&
      (glance.identity.parent == id || glance.identity.child == id)) {
    glance = {{{}, {}, glance.identity.generation}, GlancePhase::kClosed, {}};
    MarkDirty(Dirty::kTabs | Dirty::kMultiPage | Dirty::kActiveTab);
  }
}
}  // namespace kr
