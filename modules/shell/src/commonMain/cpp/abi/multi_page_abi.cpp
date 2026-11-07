#include "multi_page_abi.hpp"

#include <algorithm>
#include <cstring>

#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/layout/horizontal_layout.hpp"
#include "krypton/ui/shell/shell.hpp"
#include "payload_array.hpp"

namespace kr::abi {
namespace {
template <typename T>
constexpr std::size_t Minimum(std::size_t offset, T) noexcept {
  return offset + sizeof(T);
}
template <typename T>
void WriteElement(T* base,
                  std::size_t stride,
                  std::size_t index,
                  T value) noexcept {
  auto* bytes = reinterpret_cast<std::byte*>(base) + index * stride;
  std::size_t size = 0;
  std::memcpy(&size, bytes, sizeof(size));
  value.struct_size = size;
  std::memcpy(bytes, &value, std::min(sizeof(T), size));
}
template <typename F>
KrResult Boundary(F&& function) noexcept {
  try {
    return function();
  } catch (...) {
    return KR_ERR_PLATFORM;
  }
}
KrRect ToRect(Rect value) noexcept {
  return {value.x, value.y, value.width, value.height};
}
KrResult StateResult(const Result<void>& result) noexcept {
  return result ? KR_OK
         : result.error().code == ErrorCode::kUnsupported
             ? KR_ERR_UNSUPPORTED
             : KR_ERR_INVALID_ARGUMENT;
}
bool CanWriteGroup(const KrSplitGroup* output, std::size_t members) {
  if (!output || !KR_HAS_FIELD(output, KrSplitGroup, weight_size) ||
      output->member_count < members ||
      output->weight_count < kMaxSplitDividers)
    return false;
  return PayloadArray<KrSplitMember>::Read(
             output->members, members, output->member_size,
             Minimum(offsetof(KrSplitMember, tab), KrTabId{}))
             .has_value() &&
         PayloadArray<KrSplitWeight>::Read(
             output->weights, kMaxSplitDividers, output->weight_size,
             Minimum(offsetof(KrSplitWeight, value), float{}))
             .has_value();
}
void WriteGroup(KrSplitGroup& output,
                const std::optional<SplitGroup>& group) noexcept {
  output.id = group ? group->id.value : 0;
  output.space = group ? group->space.value : 0;
  output.revision = group ? group->revision : 0;
  output.layout =
      group ? static_cast<KrSplitLayout>(group->layout) : KR_SPLIT_COLUMNS;
  output.active_tab = group ? group->active.value : 0;
  output.member_count = group ? group->count : 0;
  output.weight_count = kMaxSplitDividers;
  for (std::size_t i = 0; i < output.member_count; ++i)
    WriteElement(output.members, output.member_size, i,
                 KrSplitMember{sizeof(KrSplitMember), group->members[i].value});
  for (std::size_t i = 0; i < kMaxSplitDividers; ++i)
    WriteElement(
        output.weights, output.weight_size, i,
        KrSplitWeight{sizeof(KrSplitWeight), group ? group->weights[i] : 0.5f});
}
KrGlanceState ToGlance(const GlanceState& state, std::size_t size) noexcept {
  return {size,
          state.identity.parent.value,
          state.identity.child.value,
          state.identity.generation,
          static_cast<KrGlancePhase>(state.phase),
          state.departure.value};
}
}  // namespace

MultiPageResult<SplitGroup> ReadSplit(const KrSplitGroup* input) {
  if (!input || !KR_HAS_FIELD(input, KrSplitGroup, weight_size) ||
      input->member_count < 2 || input->member_count > kMaxSplitMembers ||
      input->weight_count != kMaxSplitDividers)
    return std::unexpected(MultiPageError::kInvalidGroup);
  const auto members = PayloadArray<KrSplitMember>::Read(
      input->members, input->member_count, input->member_size,
      Minimum(offsetof(KrSplitMember, tab), KrTabId{}));
  const auto weights = PayloadArray<KrSplitWeight>::Read(
      input->weights, input->weight_count, input->weight_size,
      Minimum(offsetof(KrSplitWeight, value), float{}));
  if (!members || !weights)
    return std::unexpected(MultiPageError::kInvalidGroup);
  SplitGroup group{.id = SplitGroupId{input->id},
                   .space = SpaceId{input->space},
                   .revision = input->revision,
                   .layout = static_cast<SplitLayout>(input->layout),
                   .count = input->member_count,
                   .active = TabId{input->active_tab}};
  for (std::size_t i = 0; i < group.count; ++i)
    group.members[i] = TabId{(*members)[i].tab};
  for (std::size_t i = 0; i < kMaxSplitDividers; ++i)
    group.weights[i] = (*weights)[i].value;
  if (!IsValidSplit(group))
    return std::unexpected(MultiPageError::kInvalidGroup);
  return group;
}

MultiPageResult<GlanceState> ReadGlance(const KrGlanceState* input) noexcept {
  if (!input || !KR_HAS_FIELD(input, KrGlanceState, departure))
    return std::unexpected(MultiPageError::kInvalidTransition);
  const GlanceState state{
      {TabId{input->parent}, TabId{input->child}, input->generation},
      static_cast<GlancePhase>(input->phase),
      TabId{input->departure}};
  if (!IsValidGlance(state))
    return std::unexpected(MultiPageError::kInvalidTransition);
  return state;
}

namespace {
bool GlanceControlsOnLeft(void* self) noexcept {
  const auto flags = static_cast<Shell*>(self)->store().layout_prefs().flags;
  return TabLayoutOf(flags) == TabLayout::kVertical &&
         flags.Has(LayoutFlag::kSidebarRight);
}
}  // namespace

void FillMultiPageUi(KrShellUI& ui) noexcept {
  ui.show_split_options = [](void* self, KrTabId tab, KrRect anchor) noexcept {
    return Boundary([&] {
      return StateResult(static_cast<Shell*>(self)->ShowSplitOptions(
          TabId{tab}, {anchor.x, anchor.y, anchor.width, anchor.height}));
    });
  };
  ui.get_multi_page_metrics = [](void* self,
                                 KrMultiPageMetrics* output) noexcept {
    return Boundary([&] {
      if (!output || !KR_HAS_FIELD(output, KrMultiPageMetrics, motion_ms))
        return KR_ERR_INVALID_ARGUMENT;
      const auto& tokens = design::kTokens.split_view;
      output->divider_width = tokens.divider_width;
      output->glance_inset = tokens.glance_inset;
      output->glance_toolbar_height = tokens.glance_toolbar_height;
      output->motion_ms = static_cast<std::uint32_t>(tokens.motion_ms);
      // Older hosts pass a shorter record.
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, outline_width))
        output->outline_width = tokens.outline_width;
      const auto& glance = design::kTokens.glance;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_button_size))
        output->glance_button_size = glance.button_size;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_button_gap))
        output->glance_button_gap = glance.button_gap;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_controls_padding))
        output->glance_controls_padding = glance.controls_padding;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_icon_size))
        output->glance_icon_size = glance.icon_size;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_motion_ms))
        output->glance_motion_ms = static_cast<std::uint32_t>(glance.motion_ms);
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_controls_ms))
        output->glance_controls_ms =
            static_cast<std::uint32_t>(glance.controls_ms);
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_controls_offset))
        output->glance_controls_offset = glance.controls_offset;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_parent_scale))
        output->glance_parent_scale = glance.parent_scale;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_parent_opacity))
        output->glance_parent_opacity = glance.parent_opacity;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_shadow_blur))
        output->glance_shadow_blur = glance.shadow_blur;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_shadow_offset))
        output->glance_shadow_offset = glance.shadow_offset;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_button_shadow_blur))
        output->glance_button_shadow_blur = glance.button_shadow_blur;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_hover_scale))
        output->glance_hover_scale = glance.hover_scale;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_press_scale))
        output->glance_press_scale = glance.press_scale;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_disabled_opacity))
        output->glance_disabled_opacity = glance.disabled_opacity;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_controls_on_left))
        output->glance_controls_on_left = GlanceControlsOnLeft(self);
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_button_motion_ms))
        output->glance_button_motion_ms =
            static_cast<std::uint32_t>(glance.button_motion_ms);
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_close_speed))
        output->glance_close_speed = glance.close_speed;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_genie_source_scale_x))
        output->glance_genie_source_scale_x = glance.genie_source_scale_x;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_genie_source_scale_y))
        output->glance_genie_source_scale_y = glance.genie_source_scale_y;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_genie_neck_scale_x))
        output->glance_genie_neck_scale_x = glance.genie_neck_scale_x;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_genie_neck_scale_y))
        output->glance_genie_neck_scale_y = glance.genie_neck_scale_y;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_genie_bend_degrees))
        output->glance_genie_bend_degrees = glance.genie_bend_degrees;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_genie_travel))
        output->glance_genie_travel = glance.genie_travel;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_genie_first_fraction))
        output->glance_genie_first_fraction = glance.genie_first_fraction;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics,
                       glance_genie_second_fraction))
        output->glance_genie_second_fraction = glance.genie_second_fraction;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_genie_settle_scale))
        output->glance_genie_settle_scale = glance.genie_settle_scale;
      if (KR_HAS_FIELD(output, KrMultiPageMetrics, glance_genie_source_opacity))
        output->glance_genie_source_opacity = glance.genie_source_opacity;

      return KR_OK;
    });
  };
  ui.layout_glance = [](void*, KrRect parent,
                        KrGlanceGeometry* output) noexcept {
    return Boundary([&] {
      if (!output || !KR_HAS_FIELD(output, KrGlanceGeometry, page))
        return KR_ERR_INVALID_ARGUMENT;
      const auto& tokens = design::kTokens.split_view;
      const auto result =
          LayoutGlance({parent.x, parent.y, parent.width, parent.height},
                       tokens.glance_inset, tokens.glance_toolbar_height);
      if (!result)
        return KR_ERR_INVALID_ARGUMENT;
      *output = {output->struct_size, ToRect(result->frame),
                 ToRect(result->toolbar), ToRect(result->page)};
      return KR_OK;
    });
  };
  ui.layout_glance_card = [](void* self, KrRect parent,
                             KrGlanceGeometry* output) noexcept {
    return Boundary([&] {
      if (!output || !KR_HAS_FIELD(output, KrGlanceGeometry, page))
        return KR_ERR_INVALID_ARGUMENT;
      const auto& tokens = design::kTokens.glance;
      const auto result = LayoutGlance(
          {parent.x, parent.y, parent.width, parent.height},
          {tokens.width_ratio, tokens.controls_top, tokens.controls_padding,
           tokens.button_size, tokens.button_gap},
          GlanceControlsOnLeft(self));
      if (!result)
        return KR_ERR_INVALID_ARGUMENT;
      *output = {output->struct_size, ToRect(result->frame),
                 ToRect(result->toolbar), ToRect(result->page)};
      return KR_OK;
    });
  };
  ui.make_split = [](void*, const KrSplitCreateRequest* request,
                     KrSplitGroup* output) noexcept {
    return Boundary([&] {
      if (!request || !KR_HAS_FIELD(request, KrSplitCreateRequest, layout) ||
          request->member_count < 2 || request->member_count > kMaxSplitMembers)
        return KR_ERR_INVALID_ARGUMENT;
      const auto members = PayloadArray<KrSplitMember>::Read(
          request->members, request->member_count, request->member_size,
          Minimum(offsetof(KrSplitMember, tab), KrTabId{}));
      if (!members)
        return KR_ERR_INVALID_ARGUMENT;
      std::array<TabId, kMaxSplitMembers> ids{};
      for (std::size_t i = 0; i < request->member_count; ++i)
        ids[i] = TabId{(*members)[i].tab};
      const auto result =
          MakeSplit(SplitGroupId{request->id}, SpaceId{request->space},
                    std::span(ids).first(request->member_count),
                    TabId{request->active_tab},
                    static_cast<SplitLayout>(request->layout));
      if (!result || !CanWriteGroup(output, result->count))
        return KR_ERR_INVALID_ARGUMENT;
      WriteGroup(*output, std::optional(*result));
      return KR_OK;
    });
  };
  ui.plan_split_copies = [](void*, const KrSplitCopyRequest* request,
                            KrSplitCopyPlan* output) noexcept {
    return Boundary([&] {
      if (!request || !KR_HAS_FIELD(request, KrSplitCopyRequest, space) ||
          request->count < 2 || request->count > kMaxSplitMembers || !output ||
          !KR_HAS_FIELD(output, KrSplitCopyPlan, pinned) ||
          output->count < request->count)
        return KR_ERR_INVALID_ARGUMENT;
      const auto input = PayloadArray<KrTab>::Read(
          request->tabs, request->count, request->tab_size,
          Minimum(offsetof(KrTab, flags), std::uint32_t{}));
      const auto storage = PayloadArray<KrSplitCopyMember>::Read(
          output->members, request->count, output->member_size,
          Minimum(offsetof(KrSplitCopyMember, copy), bool{}));
      if (!input || !storage)
        return KR_ERR_INVALID_ARGUMENT;
      std::array<Tab, kMaxSplitMembers> tabs{};
      for (std::size_t i = 0; i < request->count; ++i) {
        const auto value = (*input)[i];
        tabs[i].id = TabId{value.id};
        tabs[i].space = SpaceId{value.space};
        tabs[i].flags = Flags<TabFlag>::FromBits(value.flags);
      }
      const auto result = PlanSplitCopies(std::span(tabs).first(request->count),
                                          SpaceId{request->space});
      if (!result)
        return KR_ERR_INVALID_ARGUMENT;
      for (std::size_t i = 0; i < result->count; ++i)
        WriteElement(
            output->members, output->member_size, i,
            KrSplitCopyMember{sizeof(KrSplitCopyMember),
                              result->sources[i].value, result->copy[i]});
      output->count = result->count;
      output->pinned = result->pinned;
      return KR_OK;
    });
  };
  ui.show_split_chooser = [](void* self) noexcept {
    return Boundary([&] {
      const auto result = static_cast<Shell*>(self)->ShowSplitChooser();
      return result ? KR_OK
             : result.error().code == ErrorCode::kUnsupported
                 ? KR_ERR_UNSUPPORTED
                 : KR_ERR_INVALID_ARGUMENT;
    });
  };
  ui.show_split_chooser_for_layout = [](void* self,
                                        KrSplitLayout layout) noexcept {
    return Boundary([&] {
      return StateResult(static_cast<Shell*>(self)->ShowSplitChooser(
          static_cast<SplitLayout>(layout)));
    });
  };
  ui.upsert_split_group = [](void* self, const KrSplitGroup* input) noexcept {
    return Boundary([&] {
      const auto group = ReadSplit(input);
      return group ? StateResult(
                         static_cast<Shell*>(self)->UpsertSplitGroup(*group))
                   : KR_ERR_INVALID_ARGUMENT;
    });
  };
  ui.remove_split_group = [](void* self, KrSplitGroupId id,
                             std::uint64_t revision) noexcept {
    return Boundary([&] {
      return StateResult(static_cast<Shell*>(self)->RemoveSplitGroup(
          SplitGroupId{id}, revision));
    });
  };
  ui.set_glance = [](void* self, const KrGlanceState* input) noexcept {
    return Boundary([&] {
      const auto glance = ReadGlance(input);
      return glance ? StateResult(static_cast<Shell*>(self)->SetGlance(*glance))
                    : KR_ERR_INVALID_ARGUMENT;
    });
  };
  // Splits and Glance start only when the host advertises them too, so the
  // host's switch decides. Geometry and reducers serve any compatible host.
  ui.multi_page_capabilities = [](void*) noexcept -> std::uint32_t {
    return KR_MULTI_PAGE_COMPLETE | KR_MULTI_PAGE_SNAP_LAYOUTS;
  };
  ui.layout_split = [](void*, const KrSplitLayoutRequest* request,
                       KrSplitGeometry* output) noexcept {
    return Boundary([&] {
      if (!request ||
          !KR_HAS_FIELD(request, KrSplitLayoutRequest, right_to_left) ||
          !output || !KR_HAS_FIELD(output, KrSplitGeometry, divider_size))
        return KR_ERR_INVALID_ARGUMENT;
      const auto group = ReadSplit(request->group);
      if (!group)
        return KR_ERR_INVALID_ARGUMENT;
      const auto page = request->page;
      const auto result =
          LayoutSplit(*group, {page.x, page.y, page.width, page.height},
                      request->divider_width, request->right_to_left);
      if (!result || output->pane_count < result->pane_count ||
          output->divider_count < result->divider_count)
        return KR_ERR_INVALID_ARGUMENT;
      const auto panes = PayloadArray<KrPaneBounds>::Read(
          output->panes, result->pane_count, output->pane_size,
          Minimum(offsetof(KrPaneBounds, bounds), KrRect{}));
      const auto dividers = PayloadArray<KrDividerBounds>::Read(
          output->dividers, result->divider_count, output->divider_size,
          Minimum(offsetof(KrDividerBounds, horizontal), bool{}));
      if (!panes || !dividers)
        return KR_ERR_INVALID_ARGUMENT;
      for (std::size_t i = 0; i < result->pane_count; ++i)
        WriteElement(
            output->panes, output->pane_size, i,
            KrPaneBounds{sizeof(KrPaneBounds), group->members[i].value,
                         ToRect(result->panes[i]),
                         ToRect(PaneHeaderBounds(
                             result->panes[i],
                             design::kTokens.split_view.pane_header_width,
                             design::kTokens.split_view.pane_header_height))});
      for (std::size_t i = 0; i < result->divider_count; ++i) {
        const auto& divider = result->dividers[i];
        WriteElement(
            output->dividers, output->divider_size, i,
            KrDividerBounds{sizeof(KrDividerBounds), ToRect(divider.bounds),
                            ToRect(divider.available), divider.index,
                            divider.axis == DividerAxis::kRows});
      }
      output->pane_count = result->pane_count;
      output->divider_count = result->divider_count;
      return KR_OK;
    });
  };
  ui.reduce_split = [](void*, const KrSplitGroup* input,
                       const KrSplitEdit* edit, KrSplitGroup* output) noexcept {
    return Boundary([&] {
      if (!edit || !KR_HAS_FIELD(edit, KrSplitEdit, weight))
        return KR_ERR_INVALID_ARGUMENT;
      const auto group = ReadSplit(input);
      if (!group)
        return KR_ERR_INVALID_ARGUMENT;
      const auto result = ReduceSplit(
          *group, {static_cast<SplitEditKind>(edit->kind), edit->revision,
                   TabId{edit->tab}, SpaceId{edit->space}, edit->index,
                   static_cast<SplitLayout>(edit->layout), edit->weight});
      if (!result || !CanWriteGroup(output, *result ? (**result).count : 0))
        return KR_ERR_INVALID_ARGUMENT;
      WriteGroup(*output, *result);
      return KR_OK;
    });
  };
  ui.reduce_glance = [](void*, const KrGlanceState* input,
                        const KrGlanceEdit* edit,
                        KrGlanceState* output) noexcept {
    return Boundary([&] {
      if (!edit || !KR_HAS_FIELD(edit, KrGlanceEdit, parent_members) ||
          !output || !KR_HAS_FIELD(output, KrGlanceState, departure))
        return KR_ERR_INVALID_ARGUMENT;
      const auto state = ReadGlance(input);
      if (!state)
        return KR_ERR_INVALID_ARGUMENT;
      const auto result = ReduceGlance(
          *state, {static_cast<GlanceEditKind>(edit->kind),
                   {TabId{edit->parent}, TabId{edit->child}, edit->generation},
                   TabId{edit->departure},
                   edit->parent_members});
      if (!result)
        return KR_ERR_INVALID_ARGUMENT;
      *output = ToGlance(*result, output->struct_size);
      return KR_OK;
    });
  };
}
}  // namespace kr::abi
