#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "core/math/math_utils.h"
#include "core/math/vec2.h"
#include "renderer/env_brdf.h"

#include <cmath>
#include <cstring>

namespace ne::test {

namespace {

// IEEE 754 binary16 to float, for finite values: the table holds no infinities or NaNs
float halfToFloat(uint16_t iHalf) {
  const int exponent = (iHalf >> 10) & 0x1F;
  const int mantissa = iHalf & 0x3FF;
  const float magnitude = exponent == 0 ? std::ldexp(static_cast<float>(mantissa), -24)
                                        : std::ldexp(static_cast<float>(1024 + mantissa), exponent - 25);
  return (iHalf & 0x8000) ? -magnitude : magnitude;
}

const ImageData& table() {
  static const ImageData sTable = buildBrdfTable();
  return sTable;
}

// (A, B) at texel (iX, iY)
Vec2 readTexel(uint32_t iX, uint32_t iY) {
  uint16_t halves[2];
  std::memcpy(halves, table().pixels.data() + (static_cast<size_t>(iY) * table().width + iX) * sizeof(halves), sizeof(halves));
  return Vec2(halfToFloat(halves[0]), halfToFloat(halves[1]));
}

float texelCenter(uint32_t iIndex) { return (static_cast<float>(iIndex) + 0.5f) / static_cast<float>(table().width); }

} // namespace

NE_TEST_CASE("env_brdf", "The Roughest Row Matches The Closed Form") {
  // At roughness 1, D is 1 / pi and the visibility 0.5 / (NdotL + NdotV), so A + B is the integral of
  // NdotL / (NdotL + NdotV) over NdotL in [0, 1]: 1 - NdotV ln((1 + NdotV) / NdotV). The row sits at roughness 0.992,
  // within 0.004 of it at these columns; UE4's Schlick-Smith visibility misses it by 0.2 at the first.
  const uint32_t y = table().height - 1;
  for (const uint32_t x : {6u, 32u}) {
    const float NdotV = texelCenter(x);
    const Vec2 ab = readTexel(x, y);
    const float expected = 1.0f - NdotV * std::log((1.0f + NdotV) / NdotV);
    NE_TEST_ASSERT(math::equals(ab.x + ab.y, expected, 0.005f), "At NdotV {}, A + B is {}, expected {}", NdotV, ab.x + ab.y,
                   expected);
  }
}

NE_TEST_CASE("env_brdf", "The Smoothest Row Reflects What Fresnel Says") {
  // A mirror reflects Schlick's F0 + (1 - F0) (1 - NdotV)^5, so A = 1 - (1 - NdotV)^5 and B = (1 - NdotV)^5
  float worstError = 0.0f;
  uint32_t worstX = 0;
  for (uint32_t x = 0; x < table().width; ++x) {
    const float Fc = math::pow(1.0f - texelCenter(x), 5.0f);
    const Vec2 ab = readTexel(x, 0);
    const float error = math::max(math::abs(ab.x - (1.0f - Fc)), math::abs(ab.y - Fc));
    if (error > worstError) {
      worstError = error;
      worstX = x;
    }
  }
  NE_TEST_ASSERT(worstError <= 0.002f, "At NdotV {}, A or B is {} off Fresnel's share", texelCenter(worstX), worstError);
}

NE_TEST_CASE("env_brdf", "No Texel Reflects More Than It Receives") {
  float smallest = 1.0f;
  float largestSum = 0.0f;
  for (uint32_t y = 0; y < table().height; ++y) {
    for (uint32_t x = 0; x < table().width; ++x) {
      const Vec2 ab = readTexel(x, y);
      smallest = math::min(smallest, math::min(ab.x, ab.y));
      largestSum = math::max(largestSum, ab.x + ab.y);
    }
  }
  NE_TEST_ASSERT(smallest >= 0.0f, "A texel holds {}", smallest);
  NE_TEST_ASSERT(largestSum <= 1.005f, "A texel's A + B is {}", largestSum); // 1 plus sampling noise
}

} // namespace ne::test

#endif
