#pragma once

#include "xaml_factory.hpp"

namespace kr::win {
// Keep native MenuFlyout command, toggle, radio and submenu behavior; only
// replace the visual layout so command icons lead and selection marks trail.
[[nodiscard]] xaml::Controls::ControlTemplate MenuItemTemplate(bool submenu,
                                                               bool checkable);
void SetMenuItemResources(const controls::Control& control);
}  // namespace kr::win
