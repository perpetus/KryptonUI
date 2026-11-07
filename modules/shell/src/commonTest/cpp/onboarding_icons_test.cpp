#include "krypton/ui/presentation/onboarding_icons.hpp"

#include "krypton/testing/test.hpp"

namespace kr {
KR_TEST(OnboardingBrowserIconsRecognizeProfilesAndChannels) {
  const auto* chrome = OnboardingBrowserIcon("Google Chrome · Personal");
  KR_ASSERT(chrome);
  KR_EXPECT_EQ(chrome->name, std::string_view("chrome"));
  const auto* beta = OnboardingBrowserIcon("google chrome beta · Work");
  KR_ASSERT(beta);
  KR_EXPECT_EQ(beta->name, std::string_view("chrome-beta"));
  KR_EXPECT(!beta->png.empty());
  KR_ASSERT(OnboardingBrowserIcon("Arc · Work"));
  KR_ASSERT(OnboardingBrowserIcon("Dia · Personal"));
  KR_EXPECT(!OnboardingBrowserIcon("Arcade profile"));
  KR_EXPECT(!OnboardingBrowserIcon("Custom browser"));
}

KR_TEST(OnboardingSearchIconsPreferKeywordsAndMatchDomainBoundaries) {
  const auto* duck =
      OnboardingSearchIcon("https://www.duckduckgo.com/search", "Google");
  KR_ASSERT(duck);
  KR_EXPECT_EQ(duck->name, std::string_view("duckduckgo"));
  const auto* bing = OnboardingSearchIcon("@BING.COM", "Web search");
  KR_ASSERT(bing);
  KR_EXPECT_EQ(bing->name, std::string_view("bing"));
  const auto* regional = OnboardingSearchIcon("google.co.nz", "Google");
  KR_ASSERT(regional);
  KR_EXPECT_EQ(regional->name, std::string_view("google"));
  const auto* localized = OnboardingSearchIcon("custom", "百度");
  KR_ASSERT(localized);
  KR_EXPECT(!localized->geometry.empty());
  KR_EXPECT(!OnboardingSearchIcon("bing.com.example.org", "Custom"));
  KR_EXPECT(!OnboardingSearchIcon("example.org", "Google-like Search"));
}
}  // namespace kr
