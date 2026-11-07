#include <algorithm>
#include <cstring>
#include <limits>

#include "krypton/fixtures/mock_browser.hpp"

namespace kr::fixtures {
MockBrowser::PageLayout MockBrowser::page_layout(bool right_to_left) const {
  PageLayout result;
  const auto append = [&](KrTabId id, KrRect bounds, bool glance) {
    if (const auto* tab = Find(id)) {
      result.surfaces[result.count++] = {
          id,
          {tab->title, tab->url, tab->color, tab->url == "krypton://blank"},
          bounds,
          glance};
    }
  };
  if (!options_.multi_page || !attached_ ||
      !KR_HAS_SLOT(&ui_, KrShellUI, layout_glance) || !ui_.layout_split ||
      !ui_.layout_glance || !ui_.get_multi_page_metrics) {
    append(active_tab_, content_bounds_, false);
    return result;
  }
  const auto parent =
      glance_.phase == KR_GLANCE_CLOSED ? active_tab_ : glance_.parent;
  KrRect parent_bounds = content_bounds_;
  const MockSplit* selected = nullptr;
  for (const auto& group : split_groups_) {
    for (std::size_t i = 0; i < group.count; ++i)
      if (group.members[i].tab == parent)
        selected = &group;
  }
  if (selected) {
    auto group = *selected;
    const auto record = group.Record();
    std::array<KrPaneBounds, 4> panes{};
    for (auto& pane : panes)
      pane.struct_size = sizeof(pane);
    for (auto& divider : result.dividers)
      divider.struct_size = sizeof(divider);
    KrMultiPageMetrics metrics{.struct_size = sizeof(KrMultiPageMetrics)};
    KrSplitGeometry geometry{
        sizeof(KrSplitGeometry), panes.data(),           panes.size(),
        sizeof(KrPaneBounds),    result.dividers.data(), result.dividers.size(),
        sizeof(KrDividerBounds)};
    if (ui_.get_multi_page_metrics(ui_.self, &metrics) == KR_OK) {
      const KrSplitLayoutRequest request{sizeof(KrSplitLayoutRequest), &record,
                                         content_bounds_, metrics.divider_width,
                                         right_to_left};
      if (ui_.layout_split(ui_.self, &request, &geometry) == KR_OK) {
        result.divider_count = geometry.divider_count;
        for (std::size_t i = 0; i < geometry.pane_count; ++i) {
          append(panes[i].tab, panes[i].bounds, false);
          if (panes[i].tab == parent)
            parent_bounds = panes[i].bounds;
        }
      }
    }
  }
  if (result.count == 0)
    append(parent, content_bounds_, false);
  const auto glance_layout =
      KR_HAS_SLOT(&ui_, KrShellUI, layout_glance_card) && ui_.layout_glance_card
          ? ui_.layout_glance_card
          : ui_.layout_glance;
  if (glance_.phase != KR_GLANCE_CLOSED &&
      glance_layout(ui_.self, parent_bounds, &result.glance) == KR_OK)
    append(glance_.child, result.glance.page, true);
  return result;
}

KrSplitGroup MockBrowser::MockSplit::Record() {
  for (auto& member : members)
    member.struct_size = sizeof(member);
  for (auto& weight : weights)
    weight.struct_size = sizeof(weight);
  return {sizeof(KrSplitGroup),
          id,
          space,
          revision,
          layout,
          active,
          members.data(),
          count,
          sizeof(KrSplitMember),
          weights.data(),
          weights.size(),
          sizeof(KrSplitWeight)};
}
void MockBrowser::MockSplit::Adopt(const KrSplitGroup& value) {
  id = value.id;
  space = value.space;
  revision = value.revision;
  layout = value.layout;
  active = value.active_tab;
  count = value.member_count;
}
MockBrowser::MockSplit* MockBrowser::FindSplit(KrTabId tab) {
  for (auto& group : split_groups_)
    for (std::size_t i = 0; i < group.count; ++i)
      if (group.members[i].tab == tab)
        return &group;
  return nullptr;
}
std::vector<KrTabId> MockBrowser::SplitMembers(KrTabId tab) const {
  for (const auto& group : split_groups_) {
    for (std::size_t i = 0; i < group.count; ++i) {
      if (group.members[i].tab != tab)
        continue;
      std::vector<KrTabId> ids;
      for (std::size_t j = 0; j < group.count; ++j)
        ids.push_back(group.members[j].tab);
      return ids;
    }
  }
  return {};
}
void MockBrowser::InitMultiPage() {
  options_.multi_page |= options_.split_panes != 0 || options_.glance;
  table_.multi_page_capabilities = [](void* self) noexcept -> std::uint32_t {
    return static_cast<MockBrowser*>(self)->options_.multi_page
               ? KR_MULTI_PAGE_COMPLETE | KR_MULTI_PAGE_SNAP_LAYOUTS
               : 0;
  };
  table_.multi_page_command = [](void* self,
                                 const KrMultiPageCommand* command) noexcept {
    try {
      return command
                 ? static_cast<MockBrowser*>(self)->MultiPageCommand(*command)
                 : KR_ERR_INVALID_ARGUMENT;
    } catch (...) {
      return KR_ERR_PLATFORM;
    }
  };
}
void MockBrowser::SeedMultiPage() {
  if (!options_.multi_page)
    return;
  if (options_.split_panes >= 2 && options_.split_panes <= 4) {
    std::array<KrTabId, 4> ids{};
    ids[0] = active_tab_;
    std::size_t count = 1;
    for (const auto& tab : tabs_) {
      if (count == options_.split_panes)
        break;
      if (tab.id != active_tab_ && tab.space == active_space_ &&
          !(tab.flags & (KR_TAB_FAVORITE | KR_TAB_PINNED)))
        ids[count++] = tab.id;
    }
    (void)CreateSplit(std::span(ids).first(count), KR_SPLIT_GRID);
  }
  if (options_.glance)
    (void)OpenGlance(active_tab_, "https://example.com/glance");
}
bool MockBrowser::CreateSplit(std::span<const KrTabId> ids,
                              KrSplitLayout layout) {
  if (!attached_ || !options_.multi_page || ids.size() < 2 || ids.size() > 4 ||
      !KR_HAS_SLOT(&ui_, KrShellUI, plan_split_copies) ||
      !ui_.plan_split_copies || !ui_.make_split)
    return false;
  if (layout < KR_SPLIT_COLUMNS || layout > KR_SPLIT_MAIN_BOTTOM)
    return false;
  std::array<KrTab, 4> tabs{};
  std::array<KrSplitCopyMember, 4> copies{};
  for (std::size_t i = 0; i < ids.size(); ++i) {
    const auto* tab = Find(ids[i]);
    if (!tab || tab->transient)
      return false;
    tabs[i] = {.struct_size = sizeof(KrTab),
               .id = tab->id,
               .space = tab->space,
               .flags = tab->flags};
    copies[i].struct_size = sizeof(copies[i]);
  }
  const KrSplitCopyRequest request{sizeof(KrSplitCopyRequest), tabs.data(),
                                   ids.size(), sizeof(KrTab), active_space_};
  KrSplitCopyPlan plan{sizeof(KrSplitCopyPlan), copies.data(), copies.size(),
                       sizeof(KrSplitCopyMember), false};
  if (ui_.plan_split_copies(ui_.self, &request, &plan) != KR_OK)
    return false;
  std::array<KrSplitMember, 4> members{};
  KrTabId active = 0;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    auto* tab = Find(ids[i]);
    KrTabId member = tab->id;
    if (copies[i].copy) {
      const auto source = *tab;
      auto& copy = AddTab(active_space_,
                          source.flags & ~(KR_TAB_FAVORITE | KR_TAB_PINNED),
                          source.title, source.url, source.color);
      member = copy.id;
      const KrTab value{sizeof(KrTab), copy.id, copy.space, copy.flags};
      ui_.tab_inserted(ui_.self, tabs_.size() - 1, &value);
      PushFavicon(copy);
      PushTab(copy);
    } else if (FindSplit(member)) {
      ForgetMultiPageTab(member);
    }
    members[i] = {sizeof(KrSplitMember), member};
    if (ids[i] == active_tab_)
      active = member;
  }
  if (!active)
    active = members[0].tab;
  MockSplit group;
  group.count = 4;
  auto output = group.Record();
  const KrSplitCreateRequest create{sizeof(KrSplitCreateRequest),
                                    next_split_id_++,
                                    active_space_,
                                    members.data(),
                                    ids.size(),
                                    sizeof(KrSplitMember),
                                    active,
                                    layout};
  if (ui_.make_split(ui_.self, &create, &output) != KR_OK)
    return false;
  group.Adopt(output);
  split_groups_.push_back(group);
  auto record = split_groups_.back().Record();
  if (ui_.upsert_split_group(ui_.self, &record) != KR_OK)
    return false;
  Activate(active);
  return true;
}

bool MockBrowser::ShowSplitOptions(KrTabId tab, KrRect anchor) {
  if (!tab)
    tab = active_tab_;
  return options_.multi_page && FindSplit(tab) &&
         KR_HAS_SLOT(&ui_, KrShellUI, show_split_options) &&
         ui_.show_split_options &&
         ui_.show_split_options(ui_.self, tab, anchor) == KR_OK;
}
void MockBrowser::PublishSplitFocus(KrTabId tab) {
  auto* group = FindSplit(tab);
  if (!group || group->active == tab)
    return;
  auto input = group->Record();
  MockSplit next;
  next.count = 4;
  auto output = next.Record();
  const KrSplitEdit edit{sizeof(KrSplitEdit), KR_SPLIT_ACTIVATE,
                         group->revision,     tab,
                         group->space,        0,
                         group->layout,       0.5f};
  if (ui_.reduce_split(ui_.self, &input, &edit, &output) != KR_OK)
    return;
  next.Adopt(output);
  *group = next;
  const auto record = group->Record();
  (void)ui_.upsert_split_group(ui_.self, &record);
}
void MockBrowser::ForgetMultiPageTab(KrTabId tab) {
  if (glance_.phase != KR_GLANCE_CLOSED && tab == glance_.parent)
    CloseGlance();
  if (glance_.phase != KR_GLANCE_CLOSED && tab == glance_.child) {
    glance_ = {sizeof(KrGlanceState), 0, 0, glance_.generation,
               KR_GLANCE_CLOSED,      0};
    if (attached_)
      (void)ui_.set_glance(ui_.self, &glance_);
  }
  auto* group = FindSplit(tab);
  if (!group || !attached_)
    return;
  const auto id = group->id;
  auto input = group->Record();
  MockSplit next;
  next.count = 4;
  auto output = next.Record();
  const KrSplitEdit edit{sizeof(KrSplitEdit), KR_SPLIT_DETACH,
                         group->revision,     tab,
                         group->space,        0,
                         group->layout,       0.5f};
  if (ui_.reduce_split(ui_.self, &input, &edit, &output) != KR_OK)
    return;
  next.Adopt(output);
  if (!next.id) {
    (void)ui_.remove_split_group(ui_.self, id, group->revision);
    std::erase_if(split_groups_,
                  [id](const MockSplit& split) { return split.id == id; });
  } else {
    *group = next;
    const auto record = group->Record();
    (void)ui_.upsert_split_group(ui_.self, &record);
  }
}
bool MockBrowser::OpenGlance(KrTabId parent, std::string_view url) {
  if (!attached_ || !options_.multi_page || !Find(parent) ||
      !(url.starts_with("https://") || url.starts_with("http://")))
    return false;
  if (glance_.phase != KR_GLANCE_CLOSED) {
    if (parent != glance_.child && parent != glance_.parent)
      return false;
    auto* child = Find(glance_.child);
    LoadInto(*child, url);
    PushTab(*child);
    PushNav();
    return true;
  }
  const auto space = Find(parent)->space;
  auto& child =
      AddTab(space, 0, "Glance preview", std::string(url), 0xFF4285F4);
  child.transient = true;
  const auto id = child.id;
  const KrTab value{sizeof(KrTab), id, space};
  ui_.tab_inserted(ui_.self, tabs_.size() - 1, &value);
  PushFavicon(child);
  PushTab(child);
  const KrGlanceEdit edit{sizeof(KrGlanceEdit),
                          KR_GLANCE_CREATE,
                          parent,
                          id,
                          glance_.generation + 1,
                          0,
                          1};
  KrGlanceState next{.struct_size = sizeof(KrGlanceState)};
  if (ui_.reduce_glance(ui_.self, &glance_, &edit, &next) != KR_OK)
    return false;
  glance_ = next;
  if (ui_.set_glance(ui_.self, &glance_) != KR_OK)
    return false;
  Activate(id);
  return true;
}
void MockBrowser::CloseGlance() {
  if (glance_.phase == KR_GLANCE_CLOSED)
    return;
  const auto previous = glance_;
  glance_ = {sizeof(KrGlanceState), 0, 0, previous.generation,
             KR_GLANCE_CLOSED,      0};
  (void)ui_.set_glance(ui_.self, &glance_);
  Close(previous.child);
  Activate(previous.parent);
}
bool MockBrowser::PromoteGlance(bool split) {
  if (glance_.phase != KR_GLANCE_OPEN)
    return false;
  const auto previous = glance_;
  const auto members = SplitMembers(previous.parent);
  if (split && members.size() == 4)
    return false;
  const KrGlanceEdit edit{sizeof(KrGlanceEdit),
                          split ? KR_GLANCE_SPLIT : KR_GLANCE_EXPAND,
                          previous.parent,
                          previous.child,
                          previous.generation,
                          0,
                          members.empty() ? 1 : members.size()};
  KrGlanceState next{.struct_size = sizeof(KrGlanceState)};
  if (ui_.reduce_glance(ui_.self, &glance_, &edit, &next) != KR_OK)
    return false;
  glance_ = next;
  Find(previous.child)->transient = false;
  (void)ui_.set_glance(ui_.self, &glance_);
  if (split) {
    if (auto* group = FindSplit(previous.parent)) {
      auto* child = Find(previous.child);
      child->space = group->space;
      if (Find(previous.parent)->flags & KR_TAB_PINNED)
        child->flags |= KR_TAB_PINNED;
      PushTab(*child);
      auto input = group->Record();
      MockSplit next_group;
      next_group.count = 4;
      auto output = next_group.Record();
      const KrSplitEdit insert{
          sizeof(KrSplitEdit), KR_SPLIT_INSERT, group->revision, previous.child,
          group->space,        group->count,    group->layout,   0.5f};
      if (ui_.reduce_split(ui_.self, &input, &insert, &output) != KR_OK)
        return false;
      next_group.Adopt(output);
      *group = next_group;
      const auto record = group->Record();
      if (ui_.upsert_split_group(ui_.self, &record) != KR_OK)
        return false;
      Activate(previous.child);
      return true;
    }
    auto selected = members;
    if (selected.empty())
      selected.push_back(previous.parent);
    selected.push_back(previous.child);
    return CreateSplit(selected, KR_SPLIT_GRID);
  }
  Activate(previous.child);
  return true;
}
KrResult MockBrowser::MultiPageCommand(const KrMultiPageCommand& command) {
  if (!options_.multi_page || !KR_HAS_FIELD(&command, KrMultiPageCommand, url))
    return KR_ERR_UNSUPPORTED;
  switch (command.kind) {
    case KR_MULTI_PAGE_SET_SPLIT_LAYOUT: {
      auto* group = FindSplit(command.tab);
      if (!group)
        return KR_ERR_INVALID_ARGUMENT;
      const KrSplitEdit edit{sizeof(KrSplitEdit), KR_SPLIT_SET_LAYOUT,
                             group->revision,     command.tab,
                             group->space,        0,
                             command.layout,      0.5f};
      KrMultiPageCommand update{.struct_size = sizeof(KrMultiPageCommand),
                                .kind = KR_MULTI_PAGE_EDIT_SPLIT,
                                .group = group->id,
                                .edit = &edit};
      return MultiPageCommand(update);
    }
    case KR_MULTI_PAGE_CHOOSE_SPLIT_LAYOUT:
      return KR_HAS_SLOT(&ui_, KrShellUI, show_split_chooser_for_layout) &&
                     ui_.show_split_chooser_for_layout
                 ? ui_.show_split_chooser_for_layout(ui_.self, command.layout)
                 : KR_ERR_UNSUPPORTED;
    case KR_MULTI_PAGE_NEW_SPLIT: {
      if (!command.member_count)
        return ui_.show_split_chooser(ui_.self);
      if (!command.members || command.member_count > 4 ||
          command.member_size < sizeof(KrSplitMember))
        return KR_ERR_INVALID_ARGUMENT;
      if (command.member_size >
          static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
              command.member_count)
        return KR_ERR_INVALID_ARGUMENT;
      std::array<KrTabId, 4> ids{};
      const auto* bytes = reinterpret_cast<const std::byte*>(command.members);
      for (std::size_t i = 0; i < command.member_count; ++i) {
        KrSplitMember member{};
        std::memcpy(&member, bytes + i * command.member_size, sizeof(member));
        if (!KR_HAS_FIELD(&member, KrSplitMember, tab) ||
            member.struct_size > command.member_size)
          return KR_ERR_INVALID_ARGUMENT;
        ids[i] = member.tab;
      }
      return CreateSplit(std::span(ids).first(command.member_count),
                         command.layout)
                 ? KR_OK
                 : KR_ERR_INVALID_ARGUMENT;
    }
    case KR_MULTI_PAGE_CLOSE_GLANCE:
      CloseGlance();
      return KR_OK;
    case KR_MULTI_PAGE_EXPAND_GLANCE:
      return PromoteGlance(false) ? KR_OK : KR_ERR_INVALID_ARGUMENT;
    case KR_MULTI_PAGE_SPLIT_GLANCE:
      return PromoteGlance(true) ? KR_OK : KR_ERR_INVALID_ARGUMENT;
    case KR_MULTI_PAGE_OPEN_GLANCE:
      if (!command.url.data && command.url.size)
        return KR_ERR_INVALID_ARGUMENT;
      return OpenGlance(command.tab, {command.url.data ? command.url.data : "",
                                      command.url.size})
                 ? KR_OK
                 : KR_ERR_INVALID_ARGUMENT;
    case KR_MULTI_PAGE_EDIT_SPLIT:
      for (auto& group : split_groups_) {
        if (group.id != command.group || !command.edit)
          continue;
        auto input = group.Record();
        MockSplit next;
        next.count = 4;
        auto output = next.Record();
        if (ui_.reduce_split(ui_.self, &input, command.edit, &output) != KR_OK)
          return KR_ERR_INVALID_ARGUMENT;
        next.Adopt(output);
        if (!next.id) {
          (void)ui_.remove_split_group(ui_.self, group.id, group.revision);
          const auto id = group.id;
          std::erase_if(split_groups_, [id](const MockSplit& value) {
            return value.id == id;
          });
        } else {
          group = next;
          const auto record = group.Record();
          (void)ui_.upsert_split_group(ui_.self, &record);
        }
        return KR_OK;
      }
      return KR_ERR_INVALID_ARGUMENT;
  }
  return KR_ERR_INVALID_ARGUMENT;
}
}  // namespace kr::fixtures
