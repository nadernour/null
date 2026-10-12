#pragma once

#include "core/math/math_utils.h"

#include <cstdint>
#include <vector>

namespace ne {

// Mip levels are packed largest first, each one half the size of the last
struct ImageData {
  uint32_t mWidth = 0;
  uint32_t mHeight = 0;
  uint32_t mChannels = 4;
  uint32_t mMipLevels = 1;
  // False for linear data such as normals, masks, and depth
  bool mSrgb = true;
  std::vector<uint8_t> mPixels{};

  size_t getLevelSizeInBytes(uint32_t iLevel) const {
    return static_cast<size_t>(math::max(1u, mWidth >> iLevel)) * math::max(1u, mHeight >> iLevel) * mChannels;
  }

  // All mip levels
  size_t getSizeInBytes() const {
    size_t size = 0;
    for (uint32_t level = 0; level < mMipLevels; ++level) {
      size += getLevelSizeInBytes(level);
    }
    return size;
  }

  static ImageData createWhite1x1() { return {.mWidth = 1, .mHeight = 1, .mPixels = {255, 255, 255, 255}}; }
  static ImageData createFlatNormal1x1() { return {.mWidth = 1, .mHeight = 1, .mSrgb = false, .mPixels = {128, 128, 255, 255}}; }
  static ImageData createBlack1x1() { return {.mWidth = 1, .mHeight = 1, .mPixels = {0, 0, 0, 255}}; }
};

} // namespace ne
