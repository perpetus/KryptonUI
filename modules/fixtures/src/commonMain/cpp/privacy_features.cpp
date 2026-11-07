#include "krypton/fixtures/mock_browser.hpp"

namespace kr::fixtures {
namespace {
KrStr Borrow(std::string_view text) noexcept {
  return {text.data(), text.size()};
}
}  // namespace
void MockBrowser::InitPrivacyFeatures() {
  table_.privacy_features_visibility_changed = [](void* self,
                                                  bool visible) noexcept {
    try {
      auto& mock = Self(self);
      mock.privacy_visible_ = visible;
      if (visible)
        mock.PushPrivacyFeatures();
    } catch (...) {
      // The preview must obey the same no-unwind C boundary as the browser.
    }
  };
  table_.set_privacy_feature = [](void* self, KrPrivacyFeatureId id,
                                  bool enabled) noexcept {
    try {
      auto& mock = Self(self);
      if (id != 1 && id != 2 && id != 10 && id != 0x10000)
        return KR_ERR_INVALID_ARGUMENT;
      if (id == 1)
        mock.privacy_widevine_ = enabled;
      else if (id == 2)
        mock.privacy_caption_ = enabled;
      else if (id == 10)
        mock.privacy_autoplay_ = enabled;
      else
        mock.privacy_search_suggestions_ = enabled;
      if (mock.privacy_visible_)
        mock.PushPrivacyFeatures();
      return KR_OK;
    } catch (...) {
      return KR_ERR_PLATFORM;
    }
  };
  table_.relaunch_browser = [](void* self) noexcept {
    try {
      auto& mock = Self(self);
      mock.privacy_running_widevine_ = mock.privacy_widevine_;
      mock.privacy_running_caption_ = mock.privacy_caption_;
      mock.privacy_running_autoplay_ = mock.privacy_autoplay_;
      if (mock.privacy_visible_)
        mock.PushPrivacyFeatures();
    } catch (...) {
    }
  };
}
void MockBrowser::PushPrivacyFeatures() const {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, upsert_privacy_feature) ||
      !ui_.upsert_privacy_feature)
    return;
  std::uint32_t flags =
      KR_PRIVACY_FEATURE_NEEDS_RESTART | KR_PRIVACY_FEATURE_ONBOARDING;
  if (privacy_widevine_)
    flags |= KR_PRIVACY_FEATURE_ENABLED;
  if (privacy_widevine_ != privacy_running_widevine_)
    flags |= KR_PRIVACY_FEATURE_RESTART_PENDING;
  const KrPrivacyFeature feature{
      sizeof(KrPrivacyFeature),
      1,
      KR_PRIVACY_GROUP_REMOTE,
      KR_PRIVACY_RISK_MODERATE,
      flags,
      1,
      Borrow("Protected video and music (Widevine DRM)"),
      Borrow("Plays media protected with Widevine. Krypton downloads Google's "
             "Widevine module to decrypt it."),
      Borrow(
          "Downloads and update checks contact Google's update servers, "
          "revealing your IP address, operating system, browser and component "
          "versions. "
          "These requests can also update the certificate revocation list. "
          "During playback, the module contacts the media service's license "
          "server with information identifying this device to that service."),
      Borrow("Moderate. Widevine runs closed-source code in a sandbox to play "
             "protected media. Its behavior cannot be independently audited."),
      Borrow("Widevine-protected streams won't play. A previously downloaded "
             "module is neither loaded nor updated. This switch does not "
             "control other media protection systems."),
      {nullptr, 0}};
  (void)ui_.upsert_privacy_feature(ui_.self, &feature);
  flags = KR_PRIVACY_FEATURE_NEEDS_RESTART;
  if (privacy_caption_)
    flags |= KR_PRIVACY_FEATURE_ENABLED;
  if (privacy_caption_ != privacy_running_caption_)
    flags |= KR_PRIVACY_FEATURE_RESTART_PENDING;
  const KrPrivacyFeature caption{
      sizeof(KrPrivacyFeature),
      2,
      KR_PRIVACY_GROUP_REMOTE,
      KR_PRIVACY_RISK_MODERATE,
      flags,
      2,
      Borrow("Live Caption"),
      Borrow(
          "Makes on-device audio captions available. After restarting, use "
          "Live Caption in the page context menu to start or stop captions."),
      Borrow("Downloads the speech engine and each chosen language pack from "
             "Google's update servers. Requests reveal your IP address, "
             "operating system, browser and component versions, and can also "
             "refresh "
             "the certificate revocation list. Caption audio is processed on "
             "this device and is not sent to a server."),
      Borrow("Moderate. The downloaded speech engine runs closed-source code "
             "locally to produce captions. Its implementation cannot be "
             "independently audited. Translation and online speech models "
             "remain disabled."),
      Borrow("Live Caption is unavailable. Its speech engine and language "
             "packs are not updated for captioning. Previously downloaded "
             "caption models remain on disk."),
      {nullptr, 0}};
  (void)ui_.upsert_privacy_feature(ui_.self, &caption);
  flags = KR_PRIVACY_FEATURE_NEEDS_RESTART;
  if (privacy_autoplay_)
    flags |= KR_PRIVACY_FEATURE_ENABLED;
  if (privacy_autoplay_ != privacy_running_autoplay_)
    flags |= KR_PRIVACY_FEATURE_RESTART_PENDING;
  const KrPrivacyFeature autoplay{
      sizeof(KrPrivacyFeature),
      10,
      KR_PRIVACY_GROUP_REMOTE,
      KR_PRIVACY_RISK_LOW,
      flags,
      10,
      Borrow("Let well-known media sites autoplay with sound"),
      Borrow("Downloads a list of media sites that can play sound without a "
             "previous engagement history."),
      Borrow(
          "Downloads and updates contact Google's component servers with your "
          "IP address, operating system, browser and component versions. These "
          "requests can also update the certificate revocation list."),
      Borrow("Low. The list changes which sites may autoplay with sound. "
             "Fetching it does not send browsing history."),
      Borrow("No downloaded preload list is used after restart. Ordinary "
             "per-site autoplay rules still apply."),
      {nullptr, 0}};
  (void)ui_.upsert_privacy_feature(ui_.self, &autoplay);
  const KrPrivacyFeature suggestions{
      sizeof(KrPrivacyFeature),
      0x10000,
      KR_PRIVACY_GROUP_REMOTE,
      KR_PRIVACY_RISK_MODERATE,
      privacy_search_suggestions_ ? KR_PRIVACY_FEATURE_ENABLED : 0u,
      20,
      Borrow("Online search suggestions"),
      Borrow("Shows suggestions from your default search engine as you type in "
             "the address bar. Off by default, as in Helium. Changes apply "
             "immediately."),
      Borrow("Sends what you type in the address bar to your default search "
             "engine "
             "before you press Enter. Requests also reveal your IP address and "
             "may include search engine cookies. Suggestion images can load "
             "from image providers named by your search engine."),
      Borrow("Moderate. Your search engine can receive incomplete searches and "
             "text you decide not to submit."),
      Borrow("Online search suggestions are disabled. Local history, bookmarks "
             "and open tabs can still appear. Searches you submit still go to "
             "your search engine."),
      {nullptr, 0}};
  (void)ui_.upsert_privacy_feature(ui_.self, &suggestions);
}
}  // namespace kr::fixtures
