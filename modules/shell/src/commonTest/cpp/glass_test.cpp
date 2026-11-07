#include "krypton/ui/model/glass.hpp"

#include <cmath>
#include <limits>

#include "krypton/testing/test.hpp"
#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/presentation/settings.hpp"

namespace kr {
KR_TEST(GlassModesPreserveOptInAndRespectAccessibilityAndPower) {
  GlassAvailability available;
  KR_EXPECT(ResolveGlassMode(GlassMode::kOff, available) == GlassMode::kOff);
  KR_EXPECT(ResolveGlassMode(GlassMode::kQuality, available) ==
            GlassMode::kQuality);
  available.energy_saver = true;
  KR_EXPECT(ResolveGlassMode(GlassMode::kQuality, available) ==
            GlassMode::kPerformance);
  available.transparency = false;
  KR_EXPECT(ResolveGlassMode(GlassMode::kPerformance, available) ==
            GlassMode::kOff);
  available.transparency = true;
  available.high_contrast = true;
  KR_EXPECT(ResolveGlassMode(GlassMode::kQuality, available) ==
            GlassMode::kOff);
  available.high_contrast = false;
  available.hardware = false;
  KR_EXPECT(ResolveGlassMode(GlassMode::kQuality, available) ==
            GlassMode::kOff);
  // Quality's transparent chrome needs no host backdrop support.
  KR_EXPECT(ResolveGlassMode(GlassMode::kQuality, {}) == GlassMode::kQuality);
}
KR_TEST(GlassSurfaceCoverageIsGatedByMode) {
  for (const auto role :
       {GlassRole::kAddress, GlassRole::kActiveTab, GlassRole::kEssential,
        GlassRole::kPanel, GlassRole::kMenu, GlassRole::kSearch}) {
    // Performance never copies, blurs or refracts a backdrop.
    KR_EXPECT(!GlassUsesLiveBackdrop(GlassMode::kOff, role));
    KR_EXPECT(!GlassUsesLiveBackdrop(GlassMode::kPerformance, role));
    KR_EXPECT(GlassUsesLiveBackdrop(GlassMode::kQuality, role) ==
              GlassUsesPanelProfile(role));
  }
  KR_EXPECT(!GlassUsesLiveBackdrop(GlassMode::kQuality, GlassRole::kAddress));
  KR_EXPECT(GlassUsesLiveBackdrop(GlassMode::kQuality, GlassRole::kSearch));
  KR_EXPECT(!GlassTransparentChrome(GlassMode::kOff));
  KR_EXPECT(!GlassTransparentChrome(GlassMode::kPerformance));
  KR_EXPECT(GlassTransparentChrome(GlassMode::kQuality));
  for (const auto role :
       {GlassRole::kAddress, GlassRole::kActiveTab, GlassRole::kEssential,
        GlassRole::kMenu, GlassRole::kSearch}) {
    KR_EXPECT(GlassEligible(GlassMode::kPerformance, role));
    KR_EXPECT(!GlassEligible(GlassMode::kOff, role));
  }
  KR_EXPECT(!GlassEligible(GlassMode::kPerformance, GlassRole::kPanel));
  KR_EXPECT(GlassEligible(GlassMode::kQuality, GlassRole::kPanel));
  KR_EXPECT(!GlassEligible(GlassMode::kOff, GlassRole::kMenu));
  KR_EXPECT(!GlassEligible(GlassMode::kOff, GlassRole::kSearch));
  KR_EXPECT(GlassUsesPanelProfile(GlassRole::kMenu));
  KR_EXPECT(GlassUsesPanelProfile(GlassRole::kSearch));
  KR_EXPECT(!GlassUsesPanelProfile(GlassRole::kAddress));
}
KR_TEST(GlassCurveHasBoundedFiniteRefractionAndAnUndistortedCenter) {
  const auto curve = MakeGlassCurve(5, 8, 1.5f, 6);
  KR_EXPECT(curve.front() > 0);
  KR_EXPECT_EQ(curve.back(), 0.0f);
  for (const auto value : curve) {
    KR_EXPECT(std::isfinite(value));
    KR_EXPECT(value >= 0 && value <= 6);
  }
  for (const auto invalid_curve :
       {MakeGlassCurve(0, 8, 1.5f, 6), MakeGlassCurve(5, 8, 1, 6),
        MakeGlassCurve(5, 8, std::numeric_limits<float>::quiet_NaN(), 6)})
    for (const auto value : invalid_curve)
      KR_EXPECT_EQ(value, 0.0f);
}
KR_TEST(GlassPreferenceEditsRoundTripAndRejectUnknownModes) {
  for (std::uint32_t mode = 0; mode <= 2; ++mode) {
    const auto edited =
        EditAppearance({}, {AppearanceOption::kGlassMode, mode});
    KR_ASSERT(edited.has_value());
    KR_EXPECT_EQ(std::to_underlying(edited->glass_mode), mode);
  }
  KR_EXPECT(!EditAppearance({}, {AppearanceOption::kGlassMode, 3}));
}

KR_TEST(GlassSearchSquircleHasASmoothBoundedInteriorAndFiniteOuterEndpoint) {
  const auto circle = MakeGlassCurve(18, 70, 1.5f, 100);
  const auto squircle =
      MakeGlassCurve(18, 70, 1.5f, 100, GlassSurfaceProfile::kConvexSquircle);
  KR_EXPECT_EQ(squircle.back(), 0.0f);
  KR_EXPECT(squircle.front() > 0);
  KR_EXPECT(squircle[128] < circle[128]);
  for (const auto value : squircle)
    KR_EXPECT(std::isfinite(value) && value >= 0 && value <= 100);
  const auto bounded =
      MakeGlassCurve(18, 70, 1.5f, 12, GlassSurfaceProfile::kConvexSquircle);
  for (const auto value : bounded)
    KR_EXPECT(value >= 0 && value <= 12);
}

KR_TEST(GlassSearchUsesTheUncappedKubeProfile) {
  const auto& t = design::kTokens.glass;
  const auto curve = MakeGlassCurve(
      t.search_bezel, t.search_thickness, t.refractive_index,
      t.search_displacement_limit, GlassSurfaceProfile::kConvexSquircle);
  KR_EXPECT_EQ(t.search_bezel, 27.0f);
  // The former 12-DIP cap flattened almost the entire curved edge. The Kube
  // profile reaches about 78 DIPs before SVG's normalization and scale ratio.
  KR_EXPECT(curve.front() > 75 && curve.front() < 80);
  KR_EXPECT(curve[64] > 12);
  KR_EXPECT(curve[192] < curve[128]);
  KR_EXPECT_EQ(curve.back(), 0.0f);
}
}  // namespace kr
