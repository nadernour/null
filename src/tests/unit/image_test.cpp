#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "core/image_data.h"
#include "core/image_utils.h"
#include "core/math/math_utils.h"

#include <array>
#include <cstring>
#include <vector>

namespace ne::test {

namespace {

// RGBA8 image holding a copy of the texels
ImageData makeImage(uint32_t iWidth, uint32_t iHeight, bool iSrgb, const std::vector<uint8_t>& iTexels) {
  return ImageData{.width = iWidth, .height = iHeight, .srgb = iSrgb, .pixels = iTexels};
}

const uint8_t* getLevelPixels(const ImageData& iImage, uint32_t iLevel) {
  size_t offset = 0;
  for (uint32_t level = 0; level < iLevel; ++level) {
    offset += iImage.getLevelSizeInBytes(level);
  }
  return iImage.pixels.data() + offset;
}

bool isWithinOne(uint8_t iValue, uint8_t iExpected) { return math::abs(static_cast<int>(iValue) - static_cast<int>(iExpected)) <= 1; }

} // namespace

NE_TEST_CASE("image", "Mip Level Count") {
  NE_TEST_ASSERT(ImageUtils::calculateMipLevels(1, 1) == 1, "1x1 has a single level.");
  NE_TEST_ASSERT(ImageUtils::calculateMipLevels(512, 512) == 10, "512x512 halves down to 1x1 in 10 levels.");
  NE_TEST_ASSERT(ImageUtils::calculateMipLevels(300, 200) == 9, "Odd halvings round down: 300x200 has 9 levels.");
  NE_TEST_ASSERT(ImageUtils::calculateMipLevels(1, 1024) == 11, "The longer side sets the count: 1x1024 has 11 levels.");
}

NE_TEST_CASE("image", "Mip Chain Layout") {
  std::vector<uint8_t> texels(5 * 3 * 4);
  for (size_t i = 0; i < texels.size(); ++i) {
    texels[i] = static_cast<uint8_t>(i);
  }
  ImageData image = makeImage(5, 3, false, texels);
  ImageData chain = ImageUtils::generateMipChain(image);

  NE_TEST_ASSERT(!chain.srgb, "The chain keeps the source color space.");
  NE_TEST_ASSERT(chain.mipLevels == 3, "5x3 chains to 2x1 and 1x1, got {} levels.", chain.mipLevels);
  NE_TEST_ASSERT(chain.getLevelSizeInBytes(1) == 2 * 1 * 4, "Level 1 is 2x1.");
  NE_TEST_ASSERT(chain.getLevelSizeInBytes(2) == 1 * 1 * 4, "Level 2 is 1x1.");
  NE_TEST_ASSERT(chain.getSizeInBytes() == (15 + 2 + 1) * 4, "Levels are packed back to back, got {} bytes.", chain.getSizeInBytes());
  NE_TEST_ASSERT(std::memcmp(chain.pixels.data(), image.pixels.data(), texels.size()) == 0, "Level 0 is copied unchanged.");
}

NE_TEST_CASE("image", "sRGB Mips Average In Linear Light") {
  std::vector<uint8_t> checker;
  for (uint32_t y = 0; y < 4; ++y) {
    for (uint32_t x = 0; x < 4; ++x) {
      const uint8_t value = (x + y) % 2 ? 255 : 0;
      checker.insert(checker.end(), {value, value, value, 255});
    }
  }
  ImageData unormChain = ImageUtils::generateMipChain(makeImage(4, 4, false, checker));
  ImageData srgbChain = ImageUtils::generateMipChain(makeImage(4, 4, true, checker));
  const uint8_t* unormTexel = getLevelPixels(unormChain, 2);
  const uint8_t* srgbTexel = getLevelPixels(srgbChain, 2);

  NE_TEST_ASSERT(isWithinOne(unormTexel[0], 128), "UNORM averages stored values: expected 128, got {}.", unormTexel[0]);
  // Linear 0.5 encodes to 188; averaging the stored sRGB values would give 128 and darken distant textures
  NE_TEST_ASSERT(isWithinOne(srgbTexel[0], 188), "sRGB averages in linear light: expected 188, got {}.", srgbTexel[0]);
  NE_TEST_ASSERT(unormTexel[3] == 255 && srgbTexel[3] == 255, "Alpha is averaged as stored, so opaque stays opaque.");
}

NE_TEST_CASE("image", "Constant Image Stays Constant") {
  // 7x5 -> 3x2 -> 1x1: odd sizes on both axes use the 3-tap weights, which must sum to one
  const std::array<uint8_t, 4> color = {200, 100, 50, 25};
  std::vector<uint8_t> texels;
  for (uint32_t i = 0; i < 7 * 5; ++i) {
    texels.insert(texels.end(), color.begin(), color.end());
  }
  for (bool srgb : {false, true}) {
    ImageData chain = ImageUtils::generateMipChain(makeImage(7, 5, srgb, texels));
    bool isConstant = true;
    for (size_t i = 0; i < chain.getSizeInBytes(); ++i) {
      isConstant = isConstant && chain.pixels[i] == color[i % 4];
    }
    NE_TEST_ASSERT(isConstant, "Every {} level keeps the source color.", srgb ? "sRGB" : "UNORM");
  }
}

NE_TEST_CASE("image", "Odd Sizes Keep Every Texel") {
  // A 2-texel box gives 128 and a bilinear blit 255; the exact box weighs all three: (0 + 255 + 0) / 3
  ImageData image = makeImage(3, 1, false, {0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255});
  ImageData chain = ImageUtils::generateMipChain(image);
  const uint8_t* texel = getLevelPixels(chain, 1);

  NE_TEST_ASSERT(texel[0] == 85, "Each texel weighs a third: expected 85, got {}.", texel[0]);
}

} // namespace ne::test

#endif
