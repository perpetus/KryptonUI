#include "native_images.hpp"

#include <robuffer.h>

#include <cstring>

namespace kr::win {

winrt::Microsoft::UI::Xaml::Media::ImageSource ImageCache::Get(
    ImageId id,
    const ImageStore* images) {
  if (!id || !images)
    return nullptr;
  return Prepare(images->Find(id));
}

winrt::Microsoft::UI::Xaml::Media::ImageSource ImageCache::Prepare(
    const Image* image) {
  if (!image)
    return nullptr;
  auto& entry = entries_[image->id];
  if (!entry.bitmap || entry.generation != image->generation) {
    entry.bitmap = winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap(
        image->width, image->height);
    const auto access =
        entry.bitmap.PixelBuffer()
            .as<::Windows::Storage::Streams::IBufferByteAccess>();
    BYTE* pixels = nullptr;
    winrt::check_hresult(access->Buffer(&pixels));
    std::memcpy(pixels, image->pixels.data(), image->pixels.size());
    entry.bitmap.Invalidate();
    entry.generation = image->generation;
  }
  return entry.bitmap;
}

void ImageCache::Drop(std::span<const ImageId> ids) {
  for (const auto id : ids)
    entries_.erase(id);
}

}  // namespace kr::win
