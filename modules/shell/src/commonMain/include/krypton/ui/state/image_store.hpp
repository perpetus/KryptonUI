#pragma once

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "krypton/ui/core/ids.hpp"

namespace kr {

// Pixels for favicons and icons: BGRA, premultiplied, tightly packed.
struct Image {
  ImageId id;
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;
  // Changes for every put, including when a dropped id is reused, so platform
  // caches keyed by (id, generation) know to convert again.
  std::uint32_t generation = 0;
};

class ImageStore {
 public:
  // Copies `pixels` (rows of `stride` bytes). Returns false for bad input.
  bool Put(ImageId id,
           int width,
           int height,
           int stride,
           const std::uint8_t* pixels);
  void Drop(ImageId id);

  [[nodiscard]] const Image* Find(ImageId id) const;

  // Ids dropped since the last ClearDropped(), so views can free their
  // native bitmaps.
  [[nodiscard]] std::span<const ImageId> dropped() const { return dropped_; }
  void ClearDropped() { dropped_.clear(); }

 private:
  std::unordered_map<ImageId, Image> images_;
  std::vector<ImageId> dropped_;
  std::uint32_t generation_ = 0;
};

}  // namespace kr
