// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Uses Zen's original GitHub and RSS paths from design/icons/logo-*.svg.
#pragma once
#include <winrt/Microsoft.UI.Xaml.Markup.h>

#include <format>

#include "krypton/ui/model/folder.hpp"
#include "xaml_factory.hpp"
namespace kr::win {
[[nodiscard]] inline controls::PathIcon LiveFolderMenuIcon(
    LiveFolderProvider provider) {
  const auto& icon = provider == LiveFolderProvider::kRss
                         ? design::icons::kLogoRss
                         : design::icons::kLogoGithub;
  return xaml::Markup::XamlReader::
      Load(std::format(
               LR"(<PathIcon xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Data="{}"/>)",
               icon.data))
          .as<controls::PathIcon>();
}
}  // namespace kr::win
