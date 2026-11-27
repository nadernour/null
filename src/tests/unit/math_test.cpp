#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "core/math/math.h"
#include "core/math/transform.h"

#include <limits>

namespace ne::test {

NE_TEST_CASE("math", "Scalar Math Utilities") {
  NE_TEST_ASSERT(math::equals(math::clamp(5.0f, 0.0f, 10.0f), 5.0f), "clamp inside range.");
  NE_TEST_ASSERT(math::equals(math::clamp(-2.0f, 0.0f, 10.0f), 0.0f), "clamp below min.");
  NE_TEST_ASSERT(math::equals(math::clamp(15.0f, 0.0f, 10.0f), 10.0f), "clamp above max.");

  NE_TEST_ASSERT(math::equals(math::lerp(0.0f, 100.0f, 0.25f), 25.0f), "scalar lerp.");

  NE_TEST_ASSERT(math::equals(math::min(3.0f, 7.0f), 3.0f), "scalar min.");
  NE_TEST_ASSERT(math::equals(math::max(3.0f, 7.0f), 7.0f), "scalar max.");
  NE_TEST_ASSERT(math::equals(math::abs(-42.0f), 42.0f), "scalar abs negative.");
  NE_TEST_ASSERT(math::equals(math::abs(42.0f), 42.0f), "scalar abs positive.");
}

NE_TEST_CASE("math", "Trigonometry & Angular Conversions") {
  NE_TEST_ASSERT(math::equals(math::radians(180.0f), math::kPi), "180 deg in rad.");
  NE_TEST_ASSERT(math::equals(math::radians(90.0f), math::kHalfPi), "90 deg in rad.");
  NE_TEST_ASSERT(math::equals(math::degrees(math::kPi), 180.0f), "pi in deg.");

  NE_TEST_ASSERT(math::equals(math::sin(0.0f), 0.0f), "sin(0).");
  NE_TEST_ASSERT(math::equals(math::sin(math::kHalfPi), 1.0f), "sin(pi/2).");
  NE_TEST_ASSERT(math::equals(math::cos(0.0f), 1.0f), "cos(0).");
  NE_TEST_ASSERT(math::equals(math::cos(math::kPi), -1.0f), "cos(pi).");
  NE_TEST_ASSERT(math::equals(math::tan(0.0f), 0.0f), "tan(0).");

  NE_TEST_ASSERT(math::equals(math::kPi * math::kInvPi, 1.0f), "pi * (1 / pi) == 1.0.");
}

NE_TEST_CASE("math", "Optics: Reflect & Refract") {
  Vec3 incident(1.0f, 0.0f, -1.0f);
  Vec3 normal(0.0f, 0.0f, 1.0f);
  Vec3 reflected = math::reflect(incident, normal);
  NE_TEST_ASSERT(reflected.equals(Vec3(1.0f, 0.0f, 1.0f)), "3D reflection against flat plane.");

  Vec3 normalIncident(0.0f, 0.0f, -1.0f);
  Vec3 refracted = math::refract(normalIncident, normal, 1.0f / 1.5f);
  NE_TEST_ASSERT(refracted.equals(normalIncident, 1e-4f), "Perpendicular incident ray passes through unbent.");

  // Total Internal Reflection (returns Vec3::Zero when eta causes sin^2(theta) > 1)
  Vec3 grazingIncident = Vec3(0.99f, 0.0f, -0.1f).getUnsafeNormal();
  Vec3 tir = math::refract(grazingIncident, normal, 2.5f); // High eta into lower index medium
  NE_TEST_ASSERT(tir.equals(Vec3::Zero), "Total internal reflection yields zero vector.");
}

NE_TEST_CASE("math", "Scalar math::equals & Member equals Tolerances") {
  NE_TEST_ASSERT(math::equals(1.00001f, 1.00002f, 1e-4f), "math::equals float with custom tolerance.");
  NE_TEST_ASSERT(!math::equals(1.00001f, 1.00002f, 1e-6f), "math::equals float failure outside tolerance.");

  NE_TEST_ASSERT(math::equals(1.00000001, 1.00000002, 1e-6), "math::equals double with custom tolerance.");
  NE_TEST_ASSERT(!math::equals(1.00000001, 1.00000002, 1e-9), "math::equals double failure outside tolerance.");

  Vec3 v1(1.0f, 2.0f, 3.0f);
  Vec3 v2(1.00005f, 2.00005f, 3.00005f);
  NE_TEST_ASSERT(v1.equals(v2, 1e-4f), "Vec3::equals member method.");

  Quat q1 = Quat::Identity;
  Quat q2(0.0f, 0.0f, 0.0f, 0.99999f);
  NE_TEST_ASSERT(q1.equals(q2, 1e-4f), "Quat::equals member method.");

  Transform t1;
  Transform t2;
  t2.position = Vec3(0.00001f, 0.0f, 0.0f);
  NE_TEST_ASSERT(t1.equals(t2, 1e-4f), "Transform::equals member method.");
}

NE_TEST_CASE("math", "Hardware-Accelerated Inverse Square Root (invSqrt)") {
  NE_TEST_ASSERT(math::equals(math::invSqrt(1.0f), 1.0f, 1e-4f), "invSqrt(1.0) == 1.0");
  NE_TEST_ASSERT(math::equals(math::invSqrt(4.0f), 0.5f, 1e-4f), "invSqrt(4.0) == 0.5");
  NE_TEST_ASSERT(math::equals(math::invSqrt(16.0f), 0.25f, 1e-4f), "invSqrt(16.0) == 0.25");
  NE_TEST_ASSERT(math::equals(math::invSqrt(100.0f), 0.1f, 1e-4f), "invSqrt(100.0) == 0.1");
  NE_TEST_ASSERT(math::equals(math::invSqrt(0.04f), 5.0f, 1e-4f), "invSqrt(0.04) == 5.0");

  const float testValues[] = {0.001f, 0.5f, 2.0f, 9.0f, 25.0f, 137.5f, 1024.0f, 65536.0f};
  for (float val : testValues) {
    float expected = 1.0f / std::sqrt(val);
    float actual = math::invSqrt(val);
    NE_TEST_ASSERT(math::equals(actual, expected, 1e-4f), "invSqrt accuracy across arbitrary scalars.");
  }
}

NE_TEST_CASE("math", "Float To Half") {
  NE_TEST_ASSERT(math::floatToHalf(0.0f) == 0x0000, "Zero.");
  NE_TEST_ASSERT(math::floatToHalf(-0.0f) == 0x8000, "Negative zero keeps its sign.");
  NE_TEST_ASSERT(math::floatToHalf(1.0f) == 0x3C00, "One.");
  NE_TEST_ASSERT(math::floatToHalf(-2.0f) == 0xC000, "Minus two.");
  NE_TEST_ASSERT(math::floatToHalf(0.307f) == 0x34E9, "0.307 rounds to the nearest half.");
  NE_TEST_ASSERT(math::floatToHalf(65504.0f) == 0x7BFF, "The largest half.");
  NE_TEST_ASSERT(math::floatToHalf(65520.0f) == 0x7C00, "Halfway past the largest half rounds to infinity.");
  NE_TEST_ASSERT(math::floatToHalf(std::numeric_limits<float>::infinity()) == 0x7C00, "Infinity.");
  NE_TEST_ASSERT((math::floatToHalf(std::numeric_limits<float>::quiet_NaN()) & 0x7FFF) > 0x7C00, "NaN stays NaN.");
  NE_TEST_ASSERT(math::floatToHalf(6.103515625e-5f) == 0x0400, "The smallest normal half, 2^-14.");
  NE_TEST_ASSERT(math::floatToHalf(5.9604645e-8f) == 0x0001, "The smallest subnormal half, 2^-24.");
  NE_TEST_ASSERT(math::floatToHalf(2.0e-8f) == 0x0000, "Below half the smallest subnormal rounds to zero.");
  // 1 + 2^-11 lies halfway between 0x3C00 and 0x3C01, and 1 + 3 * 2^-11 halfway between 0x3C01 and 0x3C02
  NE_TEST_ASSERT(math::floatToHalf(1.00048828125f) == 0x3C00, "A tie rounds to the even mantissa, down.");
  NE_TEST_ASSERT(math::floatToHalf(1.00146484375f) == 0x3C02, "A tie rounds to the even mantissa, up.");
}

} // namespace ne::test

#endif
