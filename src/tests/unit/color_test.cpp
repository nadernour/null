#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "core/math/color.h"
#include "core/math/math_utils.h"

namespace ne::test {

NE_TEST_CASE("color", "sRGB And UNORM Round Trips") {
  bool srgbRoundTrips = true;
  bool unormRoundTrips = true;
  for (uint32_t i = 0; i < 256; ++i) {
    const uint8_t value = static_cast<uint8_t>(i);
    const Color color{value, value, value, value};
    const Color srgb = LinearColor::fromSrgb(color).toSrgb();
    const Color unorm = LinearColor::fromUnorm(color).toUnorm();
    srgbRoundTrips = srgbRoundTrips && srgb.r == value && srgb.g == value && srgb.b == value && srgb.a == value;
    unormRoundTrips = unormRoundTrips && unorm.r == value && unorm.g == value && unorm.b == value && unorm.a == value;
  }
  NE_TEST_ASSERT(srgbRoundTrips, "Every 8-bit value survives an sRGB decode and encode.");
  NE_TEST_ASSERT(unormRoundTrips, "Every 8-bit value survives a UNORM decode and encode.");
}

NE_TEST_CASE("color", "sRGB Curve Applies To RGB Only") {
  const LinearColor decoded = LinearColor::fromSrgb(Color{188, 0, 255, 188});
  NE_TEST_ASSERT(math::equals(decoded.r, 0.5029f, 1e-3f), "sRGB 188 is linear ~0.503, got {}.", decoded.r);
  NE_TEST_ASSERT(decoded.g == 0.0f && decoded.b == 1.0f, "Black and white map to 0 and 1.");
  NE_TEST_ASSERT(math::equals(decoded.a, 188.0f / 255.0f), "Alpha is scaled, never curved, got {}.", decoded.a);

  const Color encoded = LinearColor{0.5f, 0.5f, 0.5f, 0.5f}.toSrgb();
  NE_TEST_ASSERT(encoded.r == 188 && encoded.a == 128, "Linear 0.5 encodes to RGB 188 and alpha 128, got {} and {}.", encoded.r,
                 encoded.a);

  const Color clamped = LinearColor{-1.0f, 2.0f, 0.0f, 3.0f}.toSrgb();
  NE_TEST_ASSERT(clamped.r == 0 && clamped.g == 255 && clamped.a == 255, "Out-of-range values clamp to [0, 1].");
}

} // namespace ne::test

#endif
