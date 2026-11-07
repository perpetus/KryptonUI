#include "menu_metadata.hpp"

#include <cstddef>
#include <limits>

#include "payload_array.hpp"

namespace kr::abi {
static_assert(std::to_underlying(MenuMetadataFlag::kActionableSubmenu) ==
              KR_MENU_META_ACTIONABLE_SUBMENU);
static_assert(std::to_underlying(MenuFontFlag::kUseBitmaps) ==
              KR_MENU_FONT_USE_BITMAPS);
namespace {

std::string Copy(KrStr text) {
  return text.data ? std::string(text.data, text.size) : std::string{};
}

}  // namespace

Result<std::optional<MenuItemMetadata>> ReadMenuMetadata(
    const KrMenuItemMetadata* input) {
  if (!input)
    return std::nullopt;
  if (!KR_HAS_FIELD(input, KrMenuItemMetadata, font_cap_height))
    return Fail(ErrorCode::kInvalidArgument, "incomplete menu metadata");

  MenuItemMetadata result{
      .source_type = input->source_type,
      .source_command_id = input->source_command_id,
      .source_index = input->source_index,
      .source_button_type = input->source_button_type,
      .source_button_index = input->source_button_index,
      .flags = Flags<MenuMetadataFlag>::FromBits(input->flags),
      .radio_group = input->radio_group,
      .separator_type = input->separator_type,
      .badge_type = input->badge_type,
      .secondary_label = Copy(input->secondary_label),
      .minor_text = Copy(input->minor_text),
      .accessible_name = Copy(input->accessible_name),
      .element_identifier = Copy(input->element_identifier),
      .button_row_label = Copy(input->button_row_label),
      .minor_icon = ImageId{input->minor_icon},
      .font_style = input->font_style,
      .font_size_pixels = input->font_size_pixels,
      .font_weight = input->font_weight,
      .font_height = input->font_height,
      .font_baseline = input->font_baseline,
      .font_cap_height = input->font_cap_height,
  };
  if (const auto* key = input->keyboard_accelerator) {
    if (!KR_HAS_FIELD(key, KrKeyboardAccelerator, interrupted_by_mouse_event))
      return Fail(ErrorCode::kInvalidArgument, "incomplete menu accelerator");
    result.keyboard_accelerator = MenuKeyboardAccelerator{
        .key_code = key->key_code,
        .modifiers = key->modifiers,
        .key_state = key->key_state,
        .time_ticks_us = key->time_ticks_us,
        .source_device_id = key->source_device_id,
        .interrupted_by_mouse_event = key->interrupted_by_mouse_event,
    };
  }
  const auto fonts = PayloadArray<KrMenuFont>::Read(
      input->fonts, input->font_count, input->font_size,
      offsetof(KrMenuFont, text_gamma) + sizeof(KrMenuFont::text_gamma));
  if (!fonts)
    return std::unexpected(fonts.error());
  result.fonts.reserve(fonts->size());
  for (std::size_t index = 0; index < fonts->size(); ++index) {
    const auto source = (*fonts)[index];
    if ((source.actual_family_count && !source.actual_families) ||
        source.actual_family_count >
            static_cast<std::size_t>(
                std::numeric_limits<std::ptrdiff_t>::max()) /
                sizeof(KrStr))
      return Fail(ErrorCode::kInvalidArgument, "invalid menu font families");
    MenuFont font{
        .family = Copy(source.family),
        .size_pixels = source.size_pixels,
        .style = source.style,
        .weight = source.weight,
        .height = source.height,
        .baseline = source.baseline,
        .cap_height = source.cap_height,
        .render_flags = Flags<MenuFontFlag>::FromBits(source.render_flags),
        .hinting = source.hinting,
        .subpixel_rendering = source.subpixel_rendering,
        .text_contrast = source.text_contrast,
        .text_gamma = source.text_gamma,
    };
    font.actual_families.reserve(source.actual_family_count);
    for (std::size_t family = 0; family < source.actual_family_count; ++family)
      font.actual_families.push_back(Copy(source.actual_families[family]));
    result.fonts.push_back(std::move(font));
  }
  const auto colors = PayloadArray<KrMenuColor>::Read(
      input->colors, input->color_count, input->color_size,
      offsetof(KrMenuColor, color) + sizeof(KrMenuColor::color));
  if (!colors)
    return std::unexpected(colors.error());
  result.colors.reserve(colors->size());
  for (std::size_t index = 0; index < colors->size(); ++index) {
    const auto color = (*colors)[index];
    result.colors.push_back({.role = color.role,
                             .semantic_id = color.semantic_id,
                             .color = color.resolved
                                          ? std::optional(Color{color.color})
                                          : std::nullopt});
  }
  return result;
}

}  // namespace kr::abi
