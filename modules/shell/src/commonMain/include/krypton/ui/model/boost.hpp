#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "krypton/ui/core/color.hpp"
#include "krypton/ui/core/ids.hpp"
#include "krypton/ui/design/tokens.hpp"

namespace kr {
enum class BoostEdit : std::uint32_t {
  kPreview,
  kCommit,
  kCancel,
  kRemove,
  kZap,
  kFocus,
  kShare
};
struct BoostDraft {
  std::uint64_t id = 0, revision = 0;
  std::uint32_t serial = 0;
  TabId tab;
  std::string name, pattern, css, zapped, focused;
  Color background, foreground, accent;
  std::uint32_t font = 0, size = 100, letter_case = 0, appearance = 0,
                match = 0;
  bool enabled = true;
  float contrast = design::kTokens.boost_editor.default_contrast;
  float brightness = design::kTokens.boost_editor.default_brightness;
  float saturation = design::kTokens.boost_editor.default_saturation;
  float palette_hue = design::kTokens.boost_editor.initial_hue;
  float palette_distance = design::kTokens.boost_editor.initial_saturation;
  float secondary_rotation = design::kTokens.boost_editor.secondary_hue_delta;
  bool invert_colors = false, color_enabled = false, auto_theme = false,
       palette_set = false;
};
struct BoostEditorState {
  bool open = false, picking = false;
  BoostDraft draft;
};
inline constexpr std::array<std::string_view, 20> kBoostFonts{
    "",
    "Arial",
    "Verdana",
    "Tahoma",
    "Trebuchet MS",
    "Segoe UI",
    "Georgia",
    "Times New Roman",
    "Palatino Linotype",
    "Cambria",
    "Consolas",
    "Cascadia Mono",
    "Lucida Console",
    "Courier New",
    "Bahnschrift",
    "Segoe Script",
    "Ink Free",
    "Gabriola",
    "Impact",
    "Comic Sans MS"};
[[nodiscard]] bool ValidBoost(const BoostDraft& value) noexcept;
[[nodiscard]] std::string BoostCss(const BoostDraft& value);
}  // namespace kr
