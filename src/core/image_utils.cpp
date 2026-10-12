#include "core/image_utils.h"
#include "core/assert.h"
#include "core/math/color.h"
#include "core/math/math_utils.h"

#include <array>
#include <bit>
#include <cstring>
#include <vector>

namespace ne::ImageUtils {

namespace {

// Source texels that box-filter into one texel of the next level along one axis
struct Taps {
  uint32_t first = 0;
  uint32_t count = 0;
  std::array<float, 3> weights{};
};

// Odd sizes spread each destination texel over 3 source texels, so every source texel keeps an equal share
Taps computeTaps(uint32_t iSrcSize, uint32_t iDst) {
  if (iSrcSize == 1) {
    return {0, 1, {1.0f, 0.0f, 0.0f}};
  }
  if (iSrcSize % 2 == 0) {
    return {2 * iDst, 2, {0.5f, 0.5f, 0.0f}};
  }
  const float n = static_cast<float>(iSrcSize);
  const float half = static_cast<float>(iSrcSize / 2);
  const float d = static_cast<float>(iDst);
  return {2 * iDst, 3, {(half - d) / n, half / n, (d + 1.0f) / n}};
}

Color loadColor(const uint8_t* iTexel) { return {iTexel[0], iTexel[1], iTexel[2], iTexel[3]}; }

void storeColor(uint8_t* oTexel, Color iColor) {
  oTexel[0] = iColor.r;
  oTexel[1] = iColor.g;
  oTexel[2] = iColor.b;
  oTexel[3] = iColor.a;
}

} // namespace

uint32_t calculateMipLevels(uint32_t iWidth, uint32_t iHeight) { return std::bit_width(math::max(iWidth, iHeight)); }

ImageData generateMipChain(const ImageData& iImage) {
  NE_ASSERT(iImage.width > 0 && iImage.height > 0 && iImage.pixels.size() >= iImage.getLevelSizeInBytes(0),
            "Image needs pixels to build a mip chain");
  NE_ASSERT(iImage.channels == 4, "Mip chains are built from RGBA8 images, got {} channels", iImage.channels);

  ImageData chain{
      .width = iImage.width,
      .height = iImage.height,
      .channels = iImage.channels,
      .mipLevels = calculateMipLevels(iImage.width, iImage.height),
      .srgb = iImage.srgb,
  };
  chain.pixels.resize(chain.getSizeInBytes());
  std::memcpy(chain.pixels.data(), iImage.pixels.data(), chain.getLevelSizeInBytes(0));

  std::vector<Taps> columnTaps;
  uint8_t* src = chain.pixels.data();
  uint32_t srcWidth = chain.width;
  uint32_t srcHeight = chain.height;
  for (uint32_t level = 1; level < chain.mipLevels; ++level) {
    const uint32_t dstWidth = math::max(1u, srcWidth / 2);
    const uint32_t dstHeight = math::max(1u, srcHeight / 2);
    uint8_t* dst = src + chain.getLevelSizeInBytes(level - 1);

    columnTaps.resize(dstWidth);
    for (uint32_t x = 0; x < dstWidth; ++x) {
      columnTaps[x] = computeTaps(srcWidth, x);
    }

    for (uint32_t y = 0; y < dstHeight; ++y) {
      const Taps rowTaps = computeTaps(srcHeight, y);
      for (uint32_t x = 0; x < dstWidth; ++x) {
        const Taps& colTaps = columnTaps[x];
        LinearColor sum{0.0f, 0.0f, 0.0f, 0.0f};
        for (uint32_t ty = 0; ty < rowTaps.count; ++ty) {
          const uint8_t* row = src + static_cast<size_t>(rowTaps.first + ty) * srcWidth * 4;
          for (uint32_t tx = 0; tx < colTaps.count; ++tx) {
            const Color texel = loadColor(row + static_cast<size_t>(colTaps.first + tx) * 4);
            const LinearColor linear = chain.srgb ? LinearColor::fromSrgb(texel) : LinearColor::fromUnorm(texel);
            sum += linear * (rowTaps.weights[ty] * colTaps.weights[tx]);
          }
        }
        storeColor(dst + (static_cast<size_t>(y) * dstWidth + x) * 4, chain.srgb ? sum.toSrgb() : sum.toUnorm());
      }
    }
    src = dst;
    srcWidth = dstWidth;
    srcHeight = dstHeight;
  }
  return chain;
}

} // namespace ne::ImageUtils
