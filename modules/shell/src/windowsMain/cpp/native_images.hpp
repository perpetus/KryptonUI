#pragma once

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <span>
#include <unordered_map>

#include "krypton/ui/state/image_store.hpp"

namespace kr::win {

class ImageCache {
 public:
  [[nodiscard]] winrt::Microsoft::UI::Xaml::Media::ImageSource Get(
      ImageId id,
      const ImageStore* images);
  void Drop(std::span<const ImageId> ids);
  [[nodiscard]] winrt::Microsoft::UI::Xaml::Media::ImageSource Prepare(
      const Image* image);

 private:
  struct Entry {
    std::uint32_t generation = 0;
    winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap bitmap{nullptr};
  };
  std::unordered_map<ImageId, Entry> entries_;
};

}  // namespace kr::win
