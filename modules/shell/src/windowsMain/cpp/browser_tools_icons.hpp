#pragma once
#include <string_view>

#include "krypton/ui/model/browser_tools.hpp"
namespace kr::win {
[[nodiscard]] constexpr std::wstring_view PermissionIcon(
    PermissionKind kind) noexcept {
  switch (kind) {
    case PermissionKind::kLocation:
      return L"\uE707";
    case PermissionKind::kCamera:
      return L"\uE722";
    case PermissionKind::kMicrophone:
      return L"\uE720";
    case PermissionKind::kNotifications:
      return L"\uEA8F";
    case PermissionKind::kClipboard:
      return L"\uE77F";
    case PermissionKind::kSound:
      return L"\uE767";
    case PermissionKind::kUsb:
      return L"\uE88E";
    case PermissionKind::kBluetooth:
      return L"\uE702";
    default:
      return L"\uE713";
  }
}
[[nodiscard]] constexpr std::wstring_view TaskIcon(
    TaskCategory category) noexcept {
  return category == TaskCategory::kExtension ? L"\uEA86"
         : category == TaskCategory::kBrowser ? L"\uE774"
                                              : L"\uE7C3";
}
}  // namespace kr::win
