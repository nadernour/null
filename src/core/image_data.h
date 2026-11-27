#pragma once

#include "core/math/math_utils.h"

#include <cstdint>
#include <vector>

namespace ne {

// Mip levels are packed largest first, each one half the size of the last
struct ImageData {
  enum class ChannelType : uint8_t {
    Unorm8,
    Float16, // math::floatToHalf bits, in native byte order; always linear
  };

  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t channels = 4;
  ChannelType channelType = ChannelType::Unorm8;
  uint32_t mipLevels = 1;
  // False for linear data such as normals, masks, and depth
  bool srgb = true;
  std::vector<uint8_t> pixels{};

  size_t getBytesPerTexel() const { return channels * (channelType == ChannelType::Float16 ? 2u : 1u); }

  size_t getLevelSizeInBytes(uint32_t iLevel) const {
    return static_cast<size_t>(math::max(1u, width >> iLevel)) * math::max(1u, height >> iLevel) * getBytesPerTexel();
  }

  // All mip levels
  size_t getSizeInBytes() const {
    size_t size = 0;
    for (uint32_t level = 0; level < mipLevels; ++level) {
      size += getLevelSizeInBytes(level);
    }
    return size;
  }

  static ImageData createWhite1x1() { return {.width = 1, .height = 1, .pixels = {255, 255, 255, 255}}; }
  static ImageData createFlatNormal1x1() { return {.width = 1, .height = 1, .srgb = false, .pixels = {128, 128, 255, 255}}; }
  static ImageData createBlack1x1() { return {.width = 1, .height = 1, .pixels = {0, 0, 0, 255}}; }
};

} // namespace ne
