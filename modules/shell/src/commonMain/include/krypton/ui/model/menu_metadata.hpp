#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "krypton/ui/core/color.hpp"
#include "krypton/ui/core/flags.hpp"
#include "krypton/ui/core/ids.hpp"

namespace kr {

// Keep source component data even when native presentation does not use it.
// Source type, style and modifier values are opaque metadata in this layer.
enum class MenuMetadataFlag : std::uint32_t {
  kVisible = 1u << 0,
  kDynamic = 1u << 1,
  kMayHaveMnemonics = 1u << 2,
  kMinorTextIsUrl = 1u << 3,
  kMinorIconOnRight = 1u << 4,
  kForceAccelerator = 1u << 5,
  kAlerted = 1u << 6,
  kNewFeature = 1u << 7,
  kButtonGroupMember = 1u << 8,
  kActionableSubmenu = 1u << 9,
  kCustomFont = 1u << 10,
};
template <>
inline constexpr bool kIsFlagEnum<MenuMetadataFlag> = true;

struct MenuKeyboardAccelerator {
  std::uint32_t key_code = 0;
  std::int32_t modifiers = 0;
  std::uint32_t key_state = 0;
  std::int64_t time_ticks_us = 0;
  std::int32_t source_device_id = 0;
  bool interrupted_by_mouse_event = false;
};

enum class MenuFontFlag : std::uint32_t {
  kAntialiasing = 1u << 0,
  kSubpixelPositioning = 1u << 1,
  kAutohinter = 1u << 2,
  kUseBitmaps = 1u << 3,
};
template <>
inline constexpr bool kIsFlagEnum<MenuFontFlag> = true;

struct MenuFont {
  std::string family;
  std::vector<std::string> actual_families;
  std::int32_t size_pixels = 0;
  std::int32_t style = 0;
  std::int32_t weight = 0;
  std::int32_t height = 0;
  std::int32_t baseline = 0;
  std::int32_t cap_height = 0;
  Flags<MenuFontFlag> render_flags;
  std::uint32_t hinting = 0;
  std::uint32_t subpixel_rendering = 0;
  float text_contrast = 0;
  float text_gamma = 0;
};

struct MenuColor {
  std::uint32_t role = 0;
  std::int32_t semantic_id = 0;
  std::optional<Color> color;
};

struct MenuItemMetadata {
  std::int32_t source_type = 0;
  std::int32_t source_command_id = 0;
  std::uint64_t source_index = 0;
  std::int32_t source_button_type = -1;
  std::uint64_t source_button_index = 0;
  Flags<MenuMetadataFlag> flags;
  std::int32_t radio_group = 0;
  std::int32_t separator_type = 0;
  std::uint32_t badge_type = 0;
  std::string secondary_label;
  std::string minor_text;
  std::string accessible_name;
  std::string element_identifier;
  std::string button_row_label;
  ImageId minor_icon;
  std::optional<MenuKeyboardAccelerator> keyboard_accelerator;
  std::vector<MenuFont> fonts;
  std::vector<MenuColor> colors;
  std::int32_t font_style = 0;
  std::int32_t font_size_pixels = 0;
  std::int32_t font_weight = 0;
  std::int32_t font_height = 0;
  std::int32_t font_baseline = 0;
  std::int32_t font_cap_height = 0;
};

}  // namespace kr
