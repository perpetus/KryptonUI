#pragma once

#include <array>
#include <memory>
#include <unordered_map>

#include "settings_style.hpp"
#include "view_context.hpp"

namespace kr::win {
class PrivacySettings {
 public:
  explicit PrivacySettings(ViewContext& context);
  PrivacySettings(ViewContext& context, const SettingsStyle& style);
  ~PrivacySettings();
  PrivacySettings(const PrivacySettings&) = delete;
  PrivacySettings& operator=(const PrivacySettings&) = delete;
  void Render(const PrivacyFeaturesVm& vm, UiTextView text, bool refresh_text);
  void InvalidateText() noexcept { text_ready_ = false; }
  [[nodiscard]] bool DialogOpen() const {
    return dialog_ && dialog_.IsLoaded();
  }
  [[nodiscard]] const controls::StackPanel& Root() const noexcept {
    return root_;
  }

 private:
  PrivacySettings(ViewContext& context, SettingsStyle style, bool styled);
  struct Row {
    controls::Border panel;
    controls::StackPanel body, labels, details;
    controls::Grid main;
    controls::ToggleSwitch toggle;
    controls::TextBlock title, summary, risk;
    controls::Expander expander;
    std::array<controls::TextBlock, 3> detail_headings, detail_text;
    controls::HyperlinkButton manage;
    controls::ToggleSwitch::Toggled_revoker toggled;
    controls::Primitives::ButtonBase::Click_revoker clicked;
  };
  void Reconcile(const PrivacyFeaturesVm& vm, UiTextView text);
  ViewContext& context_;
  SettingsStyle style_;
  bool styled_;
  controls::StackPanel root_;
  controls::TextBlock heading_;
  std::array<controls::StackPanel, 3> groups_;
  std::array<controls::TextBlock, 3> headings_;
  controls::InfoBar restart_, error_;
  controls::Button relaunch_;
  controls::Primitives::ButtonBase::Click_revoker relaunched_;
  std::unordered_map<PrivacyFeatureId, std::unique_ptr<Row>> rows_;
  controls::ContentDialog dialog_{nullptr};
  std::optional<PrivacyFeatureId> confirmation_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  std::uint64_t revision_ = ~std::uint64_t{0};
  bool reconciling_ = false, text_ready_ = false;
};
}  // namespace kr::win
