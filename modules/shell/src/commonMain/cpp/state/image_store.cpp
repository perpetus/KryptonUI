#include "krypton/ui/state/image_store.hpp"

#include <algorithm>
#include <cstring>

namespace kr {

namespace {
// Favicons and action icons are small; refuse anything absurd rather than
// letting a host bug allocate gigabytes on the UI thread.
constexpr int kMaxImageSide = 1024;
}  // namespace

bool ImageStore::Put(ImageId id,
                     int width,
                     int height,
                     int stride,
                     const std::uint8_t* pixels) {
  if (!id || width <= 0 || height <= 0 || width > kMaxImageSide ||
      height > kMaxImageSide || stride < width * 4 || pixels == nullptr) {
    return false;
  }

  Image& image = images_[id];
  image.id = id;
  image.width = width;
  image.height = height;
  image.generation = ++generation_;

  const std::size_t row_bytes = static_cast<std::size_t>(width) * 4;
  image.pixels.resize(row_bytes * static_cast<std::size_t>(height));
  for (int row = 0; row < height; ++row) {
    std::memcpy(image.pixels.data() + row_bytes * row,
                pixels + static_cast<std::size_t>(stride) * row, row_bytes);
  }

  // A re-sent id is alive again; don't let a pending drop evict it.
  std::erase(dropped_, id);
  return true;
}

void ImageStore::Drop(ImageId id) {
  if (images_.erase(id) != 0) {
    dropped_.push_back(id);
  }
}

const Image* ImageStore::Find(ImageId id) const {
  const auto it = images_.find(id);
  return it == images_.end() ? nullptr : &it->second;
}

}  // namespace kr
