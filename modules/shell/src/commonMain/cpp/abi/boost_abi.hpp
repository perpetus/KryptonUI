#pragma once
#include <optional>

#include "krypton/ui/model/boost.hpp"
#include "krypton_ui/abi.h"

namespace kr::abi {
inline std::optional<BoostDraft> ReadBoost(const KrBoost* input) {
  if (!input || !KR_HAS_FIELD(input, KrBoost, enabled))
    return std::nullopt;
  for (auto value :
       {input->name, input->pattern, input->css, input->zapped, input->focused})
    if ((!value.data && value.size) || value.size > 256 * 1024)
      return std::nullopt;
  const auto copy = [](KrStr value) {
    return value.data ? std::string(value.data, value.size) : std::string();
  };
  BoostDraft value{input->id,
                   input->revision,
                   input->serial,
                   TabId{input->tab},
                   copy(input->name),
                   copy(input->pattern),
                   copy(input->css),
                   copy(input->zapped),
                   copy(input->focused),
                   Color{input->background},
                   Color{input->foreground},
                   Color{input->accent},
                   input->font,
                   input->size,
                   input->letter_case,
                   input->appearance,
                   input->match,
                   input->enabled};
  if (KR_HAS_FIELD(input, KrBoost, palette_set)) {
    value.contrast = input->contrast;
    value.brightness = input->brightness;
    value.saturation = input->saturation;
    value.palette_hue = input->palette_hue;
    value.palette_distance = input->palette_distance;
    value.secondary_rotation = input->secondary_rotation;
    value.invert_colors = input->invert_colors;
    value.color_enabled = input->color_enabled;
    value.auto_theme = input->auto_theme;
    value.palette_set = input->palette_set;
  } else {
    value.color_enabled = value.background.a() || value.foreground.a();
  }
  return ValidBoost(value) ? std::optional(std::move(value)) : std::nullopt;
}
inline KrBoost WriteBoost(const BoostDraft& v, std::string_view compiled) {
  const auto str = [](std::string_view s) -> KrStr {
    return {s.data(), s.size()};
  };
  return {sizeof(KrBoost),
          v.id,
          v.revision,
          v.tab.value,
          v.serial,
          str(v.name),
          str(v.pattern),
          str(v.css),
          str(compiled),
          str(v.zapped),
          str(v.focused),
          v.background.argb,
          v.foreground.argb,
          v.accent.argb,
          v.font,
          v.size,
          v.letter_case,
          v.appearance,
          v.match,
          v.enabled,
          v.contrast,
          v.brightness,
          v.saturation,
          v.palette_hue,
          v.palette_distance,
          v.secondary_rotation,
          v.invert_colors,
          v.color_enabled,
          v.auto_theme,
          v.palette_set};
}
}  // namespace kr::abi
