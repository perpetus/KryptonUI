// Native XAML API regressions. These tests never create or show a window.
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <utility>

#include "icon_view.hpp"
#include "krypton/testing/test.hpp"
#include "media_slider.hpp"
#include "native_motion.hpp"
#include "native_theme.hpp"
#include "sprite_icon.hpp"
#include "winui_runtime.hpp"

namespace kr::win {
namespace {
void EnsureNativeAssets() {
  const auto result = windows::EnsureRuntime();
  if (!result)
    std::fprintf(stderr, "Native asset runtime: %s\n",
                 result.error().message.c_str());
  KR_ASSERT(result.has_value());
}

template <typename Action>
void ExpectNative(Action&& action) {
  try {
    std::forward<Action>(action)();
  } catch (const winrt::hresult_error& error) {
    std::fprintf(stderr, "Native asset API: %s (0x%08X)\n",
                 winrt::to_string(error.message()).c_str(),
                 static_cast<unsigned>(error.code().value));
    KR_ASSERT(false);
  }
}

KR_TEST(NativeIconPathsConstructAndSwapEveryPackWithoutAWindow) {
  EnsureNativeAssets();
  ExpectNative([] {
    Brushes brushes;
    NativeMotion motion;
    IconSet icons(motion, brushes);
    IconView view(icons, IconRole::kBack, 16, brushes.icon);
    for (std::size_t role = 0; role < design::icon_packs::kRoleCount; ++role) {
      view.SetRole(static_cast<IconRole>(role));
      // Unattached icons apply their shape without creating a crossfade.
      KR_EXPECT_EQ(view.Root().Children().Size(), 1u);
      for (std::size_t pack = 0; pack < kIconPackCount; ++pack)
        icons.SetPack(static_cast<IconPack>(pack), false);
    }
    icons.SetPack(IconPack::kFluent, false);
    view.SetRole(IconRole::kPlay);
    view.SetRole(IconRole::kPause);
    view.SetRole(IconRole::kPlay);
    icons.SetPack(IconPack::kFluent, false);
  });
}

KR_TEST(NativeLibrarySpritesCanPlayAndReverseWithoutAWindow) {
  EnsureNativeAssets();
  ExpectNative([] {
    for (const auto sprite :
         {Sprite::kLibrary, Sprite::kMedia, Sprite::kDownloads, Sprite::kBoosts,
          Sprite::kSpaces, Sprite::kHistory}) {
      SpriteIcon icon(sprite, 28);
      KR_EXPECT(icon.Play(0, 35, 350, true));
      KR_EXPECT(icon.Play(35, 0, 300, true));
      KR_EXPECT(icon.Play(0, 35, 583, false));
    }
  });
}

KR_TEST(NativeMediaSliderMetricsAndRevealUseTheStockTemplateTypes) {
  EnsureNativeAssets();
  ExpectNative([] {
    Brushes brushes;
    controls::Slider slider;
    SetMediaSliderResources(slider, brushes.pill);
    for (const auto key :
         {L"SliderTrackThemeHeight", L"SliderPreContentMargin",
          L"SliderPostContentMargin", L"SliderHorizontalThumbWidth",
          L"SliderHorizontalThumbHeight", L"SliderInnerThumbWidth",
          L"SliderInnerThumbHeight", L"SliderHorizontalHeight"}) {
      const auto value = slider.Resources()
                             .Lookup(winrt::box_value(winrt::hstring(key)))
                             .as<winrt::Windows::Foundation::IPropertyValue>();
      KR_EXPECT(value.Type() ==
                winrt::Windows::Foundation::PropertyType::Double);
      KR_EXPECT(value.GetDouble() > 0);
    }
    controls::Grid row;
    row.Children().Append(slider);
    for (int reveal = 0; reveal < 2; ++reveal) {
      row.Visibility(xaml::Visibility::Collapsed);
      row.Visibility(xaml::Visibility::Visible);
      (void)slider.ApplyTemplate();
      row.Measure({320, 100});
      row.Arrange({0, 0, 320, 100});
      slider.Value(50);
    }
    // The expanded stack animates these targets from their retained values.
    // Exercise the expression-keyframe path used by pointer hover as well.
    const auto compositor =
        xaml::Media::CompositionTarget::GetCompositorForCurrentThread();
    const auto easing =
        compositor.CreateCubicBezierEasingFunction({0.25f, 1}, {0.5f, 1});
    for (const auto target : {L"Translation", L"Scale"}) {
      auto animation = compositor.CreateVector3KeyFrameAnimation();
      animation.Target(target);
      animation.InsertExpressionKeyFrame(0, L"this.StartingValue");
      animation.InsertKeyFrame(1, {1, 1, 1}, easing);
      animation.Duration(std::chrono::milliseconds(300));
      row.StartAnimation(animation);
      row.StopAnimation(animation);
    }
    auto fade = compositor.CreateScalarKeyFrameAnimation();
    fade.Target(L"Opacity");
    fade.InsertExpressionKeyFrame(0, L"this.StartingValue");
    fade.InsertKeyFrame(1, 1, easing);
    fade.Duration(std::chrono::milliseconds(300));
    row.StartAnimation(fade);
    row.StopAnimation(fade);
  });
}

KR_TEST(NativeOnboardingBatchesCanRegisterCancelAndRestartWithoutAWindow) {
  EnsureNativeAssets();
  ExpectNative([] {
    const auto compositor =
        xaml::Media::CompositionTarget::GetCompositorForCurrentThread();
    int completions = 0;
    {
      MotionBatch batch;
      batch.Begin(compositor);
      batch.End([&] { ++completions; });
      batch.Cancel();
      batch.Begin(compositor);
      batch.End([&] { ++completions; });
      // Destruction must unregister a pending completion as well.
    }
    KR_EXPECT_EQ(completions, 0);
  });
}

KR_TEST(NativeDownloadHoverAnimationsAcceptTheirTargetsAndDelay) {
  EnsureNativeAssets();
  ExpectNative([] {
    controls::Grid entry;
    const auto visual =
        xaml::Hosting::ElementCompositionPreview::GetElementVisual(entry);
    const auto compositor = visual.Compositor();
    auto rise = compositor.CreateVector3KeyFrameAnimation();
    rise.Target(L"Translation");
    const auto linear = compositor.CreateLinearEasingFunction();
    for (const auto& [at, progress] :
         std::array<std::pair<float, float>, 6>{{{0.0f, 0.0f},
                                                 {0.12f, 0.34f},
                                                 {0.24f, 0.7f},
                                                 {0.38f, 0.93f},
                                                 {0.55f, 1.03f},
                                                 {1.0f, 1.0f}}})
      rise.InsertKeyFrame(at, {0, 25 * (1 - progress), 0}, linear);
    rise.Duration(std::chrono::milliseconds(220));
    rise.DelayTime(std::chrono::milliseconds(12));
    rise.DelayBehavior(winrt::Microsoft::UI::Composition::
                           AnimationDelayBehavior::SetInitialValueBeforeDelay);
    entry.StartAnimation(rise);
    auto fade = compositor.CreateScalarKeyFrameAnimation();
    fade.Target(L"Opacity");
    fade.InsertKeyFrame(0, 0);
    fade.InsertKeyFrame(1, 1);
    fade.Duration(std::chrono::milliseconds(220));
    entry.StartAnimation(fade);
    entry.StopAnimation(rise);
    entry.StopAnimation(fade);

    controls::StackPanel media;
    media.OpacityTransition(xaml::ScalarTransition());
    media.OpacityTransition().Duration(std::chrono::milliseconds(300));
    media.Opacity(0);
    media.IsHitTestVisible(false);

    const auto clip = compositor.CreateInsetClip();
    visual.Clip(clip);
    const auto ease =
        compositor.CreateCubicBezierEasingFunction({0.22f, 1}, {0.36f, 1});
    auto clipping = compositor.CreateScalarKeyFrameAnimation();
    clipping.InsertExpressionKeyFrame(0, L"this.StartingValue");
    clipping.InsertKeyFrame(1, 200, ease);
    clipping.Duration(std::chrono::milliseconds(300));
    clip.StartAnimation(L"BottomInset", clipping);
  });
}
}  // namespace
}  // namespace kr::win
