#include "sprite_icon.hpp"

#include <shcore.h>
#include <shlwapi.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>

#include "krypton/ui/design/sprites.generated.hpp"
#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
namespace {
namespace sprites = design::sprites;
constexpr std::string_view kFillOpacity = "KRFILLOPACITY";
constexpr std::string_view kFill = "KRFILL";
constexpr std::string_view kStroke = "KRSTROKE";

std::span<const std::string_view> ChunksFor(Sprite sprite) noexcept {
  switch (sprite) {
    case Sprite::kLibrary:
      return sprites::kLibrarySprite;
    case Sprite::kMedia:
      return sprites::kLibraryMediaSprite;
    case Sprite::kDownloads:
      return sprites::kLibraryDownloadsSprite;
    case Sprite::kBoosts:
      return sprites::kLibraryBoostsSprite;
    case Sprite::kSpaces:
      return sprites::kLibrarySpacesSprite;
    case Sprite::kRouting:
      return sprites::kLibraryRoutingSprite;
    case Sprite::kBoostIndicator:
      return sprites::kBoostIndicatorSprite;
    case Sprite::kHistory:
      break;
  }
  return sprites::kLibraryHistorySprite;
}

std::string Hex(Color color) {
  return std::format("#{:02X}{:02X}{:02X}", color.r(), color.g(), color.b());
}
}  // namespace

SpriteIcon::SpriteIcon(Sprite sprite, double size)
    : chunks_(ChunksFor(sprite)),
      size_(size),
      frames_(sprite == Sprite::kBoostIndicator ? sprites::kBoostIndicatorFrames
                                                : sprites::kFrames) {
  root_.Width(size);
  root_.Height(size);
  root_.IsHitTestVisible(false);
  media::RectangleGeometry window;
  window.Rect({0, 0, static_cast<float>(size), static_cast<float>(size)});
  root_.Clip(window);
  image_.Width(size * frames_);
  image_.Height(size);
  image_.Stretch(media::Stretch::Fill);
  image_.Source(source_);
  strip_.Width(size * frames_);
  strip_.Height(size);
  strip_.Children().Append(image_);
  root_.Children().Append(strip_);
  if (sprite == Sprite::kLibrary) {
    // Zen's box never changes between frames. Keep it outside the moving
    // atlas so it remains visible while the decoration/glow strip steps.
    box_source_ = xaml::Media::Imaging::SvgImageSource();
    box_image_ = controls::Image();
    box_image_.Width(size);
    box_image_.Height(size);
    box_image_.Stretch(media::Stretch::Fill);
    box_image_.Source(box_source_);
    root_.Children().Append(box_image_);
  }
}

void SpriteIcon::SetColors(Color stroke, Color fill, float scale, bool glow) {
  if (stroke != stroke_ || fill != fill_ || scale != scale_) {
    stroke_ = stroke;
    fill_ = fill;
    scale_ = scale;
    Load();
  }
  glow_enabled_ = glow;
  if (glow && glow_scale_ != scale)
    LoadGlow();
  if (glow_visual_)
    glow_visual_.IsVisible(glow);
}

void SpriteIcon::Load() {
  LoadColored(source_, chunks_, frames_);
  if (box_image_)
    LoadColored(box_source_, sprites::kLibraryBox, 1);
}

void SpriteIcon::LoadColored(const xaml::Media::Imaging::SvgImageSource& source,
                             std::span<const std::string_view> chunks,
                             int frames) {
  // Theme colours replace Zen's context-fill/-stroke tokens.
  const auto fill = fill_.a() ? Hex(fill_) : std::string("none");
  const auto opacity = std::format("{:.3f}", fill_.a() / 255.0f);
  const auto stroke = Hex(stroke_);
  std::string raw;
  std::size_t size = 0;
  for (const auto chunk : chunks)
    size += chunk.size();
  raw.reserve(size);
  for (const auto chunk : chunks)
    raw += chunk;
  svg_.clear();
  svg_.reserve(size);
  for (std::size_t at = 0;;) {
    const auto hit = raw.find("KR", at);
    svg_.append(raw, at,
                hit == std::string::npos ? std::string::npos : hit - at);
    if (hit == std::string::npos)
      break;
    const std::string_view rest = std::string_view(raw).substr(hit);
    if (rest.starts_with(kFillOpacity)) {
      svg_ += opacity;
      at = hit + kFillOpacity.size();
    } else if (rest.starts_with(kFill)) {
      svg_ += fill;
      at = hit + kFill.size();
    } else if (rest.starts_with(kStroke)) {
      svg_ += stroke;
      at = hit + kStroke.size();
    } else {
      svg_ += "KR";
      at = hit + 2;
    }
  }
  LoadSource(source, svg_, frames);
}

void SpriteIcon::LoadSource(const xaml::Media::Imaging::SvgImageSource& source,
                            std::string_view svg,
                            int frames) {
  try {
    source.RasterizePixelWidth(size_ * frames * scale_);
    source.RasterizePixelHeight(size_ * scale_);
    // A memory stream keeps this free of file I/O; SvgImageSource parses and
    // rasterizes it asynchronously and owns the stream until it is done.
    winrt::com_ptr<IStream> memory;
    memory.attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(svg.data()),
                                    static_cast<UINT>(svg.size())));
    if (!memory)
      return;
    winrt::Windows::Storage::Streams::IRandomAccessStream stream{nullptr};
    winrt::check_hresult(CreateRandomAccessStreamOverStream(
        memory.get(), BSOS_DEFAULT,
        winrt::guid_of<winrt::Windows::Storage::Streams::IRandomAccessStream>(),
        winrt::put_abi(stream)));
    [[maybe_unused]] const auto loading = source.SetSourceAsync(stream);
  } catch (const winrt::hresult_error& error) {
    platform::WriteDefaultLog(
        LogLevel::kWarning,
        std::format("Library sprite could not load (0x{:08X})",
                    static_cast<std::uint32_t>(error.code().value)));
  }
}

void SpriteIcon::LoadGlow() {
  if (!glow_image_) {
    glow_source_ = xaml::Media::Imaging::SvgImageSource();
    glow_image_ = controls::Image();
    glow_image_.Width(size_ * sprites::kFrames);
    glow_image_.Height(size_);
    glow_image_.Stretch(media::Stretch::Fill);
    glow_image_.Source(glow_source_);
    // GetAlphaMask prints SVGs into an A8 target, which Direct2D rejects.
    // Capture the normally rasterized image instead. Hide only its parent's
    // native visual: XAML still updates the image, and the captured subtree
    // retains its own full opacity without being drawn directly on screen.
    glow_host_ = controls::Canvas();
    glow_host_.Width(size_ * sprites::kFrames);
    glow_host_.Height(size_);
    glow_host_.Children().Append(glow_image_);
    strip_.Children().Append(glow_host_);
    xaml::Hosting::ElementCompositionPreview::GetElementVisual(glow_host_)
        .Opacity(0);
    const auto compositor =
        media::CompositionTarget::GetCompositorForCurrentThread();
    glow_visual_ = compositor.CreateSpriteVisual();
    glow_visual_.Size({static_cast<float>(size_ * sprites::kFrames),
                       static_cast<float>(size_)});
    glow_visual_.IsVisible(glow_enabled_);
    glow_shadow_ = compositor.CreateDropShadow();
    glow_shadow_.BlurRadius(kTokens.library.button_glow_blur *
                            static_cast<float>(size_) /
                            kTokens.library.button_sprite_size);
    glow_shadow_.Offset({0, 0, 0});
    glow_shadow_.Color(NativeColor(Color{kTokens.library.button_glow_color}));
    glow_shadow_.Opacity(1);
    glow_surface_ = compositor.CreateVisualSurface();
    glow_surface_.SourceVisual(
        xaml::Hosting::ElementCompositionPreview::GetElementVisual(
            glow_image_));
    glow_surface_.SourceSize({static_cast<float>(size_ * sprites::kFrames),
                              static_cast<float>(size_)});
    glow_brush_ = compositor.CreateSurfaceBrush(glow_surface_);
    glow_shadow_.Mask(glow_brush_);
    glow_visual_.Shadow(glow_shadow_);
    xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(
        strip_, glow_visual_);
  }
  glow_scale_ = scale_;
  svg_.clear();
  for (const auto chunk : sprites::kLibraryGlowSprite)
    svg_ += chunk;
  LoadSource(glow_source_, svg_, sprites::kFrames);
}

void SpriteIcon::ShowFrame(int frame) {
  looping_ = false;
  if (animation_) {
    strip_.StopAnimation(animation_);
    animation_ = nullptr;
  }
  frame_ = std::clamp(frame, 0, frames_ - 1);
  strip_.Translation({-static_cast<float>(frame_ * size_), 0, 0});
}

int SpriteIcon::CurrentFrame() const noexcept {
  if (!animation_ || duration_.count() <= 0)
    return frame_;
  const auto elapsed = std::chrono::steady_clock::now() - started_;
  if (elapsed >= duration_)
    return frame_;
  const int count = std::abs(to_frame_ - from_frame_) + 1;
  const float progress = std::chrono::duration<float>(elapsed).count() /
                         std::chrono::duration<float>(duration_).count();
  const int index = std::min(count - 1, static_cast<int>(progress * count));
  return from_frame_ + (to_frame_ > from_frame_ ? index : -index);
}

bool SpriteIcon::PlayTo(int frame, float full_duration_ms) noexcept {
  const int current = CurrentFrame();
  const float duration =
      full_duration_ms * std::abs(frame - current) / (frames_ - 1);
  return Play(current, frame, duration, true);
}

SpriteIcon::~SpriteIcon() {
  (void)Play(0, 0, 0, false);
}
bool SpriteIcon::Loop(bool running, float milliseconds) noexcept {
  if (running == looping_)
    return true;
  return Play(0, running ? frames_ - 1 : 0, running ? milliseconds : 0, false,
              running);
}
bool SpriteIcon::Play(int from,
                      int to,
                      float milliseconds,
                      bool hold,
                      bool repeat) noexcept try {
  const int end = hold ? to : 0;
  ShowFrame(end);
  if (from == to || milliseconds <= 0)
    return true;
  const auto compositor =
      media::CompositionTarget::GetCompositorForCurrentThread();
  const auto step_easing = compositor.CreateStepEasingFunction(1);
  step_easing.IsInitialStepSingleFrame(false);
  step_easing.IsFinalStepSingleFrame(true);
  auto animation = compositor.CreateVector3KeyFrameAnimation();
  animation.Target(L"Translation");
  // A native step easing holds each frame until the next boundary. Keep one
  // key per frame rather than approximating steps with pairs of close keys.
  const int count = std::abs(to - from) + 1;
  const int step = to > from ? 1 : -1;
  const auto offset = [&](int frame) {
    return winrt::Windows::Foundation::Numerics::float3{
        -static_cast<float>(frame * size_), 0, 0};
  };
  for (int index = 0; index < count; ++index) {
    const int frame = from + step * index;
    const float start = static_cast<float>(index) / count;
    animation.InsertKeyFrame(start, offset(frame), step_easing);
  }
  animation.InsertKeyFrame(1, offset(end), step_easing);
  duration_ =
      std::chrono::milliseconds(std::max(1, static_cast<int>(milliseconds)));
  animation.Duration(duration_);
  if (repeat)
    animation.IterationBehavior(
        winrt::Microsoft::UI::Composition::AnimationIterationBehavior::Forever);
  from_frame_ = from;
  to_frame_ = to;
  started_ = std::chrono::steady_clock::now();
  animation_ = animation;
  strip_.StartAnimation(animation_);
  looping_ = repeat;
  return true;
} catch (const winrt::hresult_error& error) {
  platform::WriteDefaultLog(
      LogLevel::kWarning,
      std::format("Library sprite animation: {} (0x{:08X})",
                  winrt::to_string(error.message()),
                  static_cast<std::uint32_t>(error.code().value)));
  return false;
} catch (...) {
  platform::WriteDefaultLog(LogLevel::kWarning,
                            "Library sprite animation could not start");
  return false;
}
}  // namespace kr::win
