#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "core/math/math.h"

namespace ne::test {

NE_TEST_CASE("vector", "Vec2 Operations & Member Functions") {
  Vec2 a(3.0f, 4.0f);
  Vec2 b(1.0f, 2.0f);

  NE_TEST_ASSERT(math::equals(a.length(), 5.0f), "Vec2 member length of (3,4) must be 5.");
  NE_TEST_ASSERT(math::equals(a.lengthSquared(), 25.0f), "Vec2 member lengthSquared must be 25.");
  NE_TEST_ASSERT(a.getUnsafeNormal().isNormalized(), "Vec2 getUnsafeNormal must be normalized.");
  NE_TEST_ASSERT(Vec2::Zero.getSafeNormal(math::kSmallNumber, Vec2::UnitX).equals(Vec2::UnitX),
                 "Safe normalize zero vector must return fallback.");
  NE_TEST_ASSERT(math::equals(Vec2(1.0f, 0.0f).dot(Vec2(0.0f, 1.0f)), 0.0f),
                 "Member dot product of orthogonal vectors must be 0.");

  NE_TEST_ASSERT(math::equals(a[0], 3.0f) && math::equals(a[1], 4.0f), "Vec2 operator[] indexing.");
  NE_TEST_ASSERT(a.data() == &a.x, "Vec2 data() pointer points to x component.");

  NE_TEST_ASSERT((a + b).equals(Vec2(4.0f, 6.0f)), "Vec2 addition operator.");
  NE_TEST_ASSERT((a - b).equals(Vec2(2.0f, 2.0f)), "Vec2 subtraction operator.");
  NE_TEST_ASSERT((a * b).equals(Vec2(3.0f, 8.0f)), "Vec2 element-wise multiplication operator.");
  NE_TEST_ASSERT((a / b).equals(Vec2(3.0f, 2.0f)), "Vec2 element-wise division operator.");
  NE_TEST_ASSERT((a * 2.0f).equals(Vec2(6.0f, 8.0f)), "Vec2 scalar multiplication operator.");
  NE_TEST_ASSERT((2.0f * a).equals(Vec2(6.0f, 8.0f)), "Vec2 free scalar multiplication operator.");
  NE_TEST_ASSERT((a + 1.0f).equals(Vec2(4.0f, 5.0f)), "Vec2 scalar bias addition operator.");
  NE_TEST_ASSERT((1.0f + a).equals(Vec2(4.0f, 5.0f)), "Vec2 free scalar bias addition operator.");
  NE_TEST_ASSERT((-a).equals(Vec2(-3.0f, -4.0f)), "Vec2 unary negation operator.");

  Vec2 compound(1.0f, 2.0f);
  compound += Vec2(2.0f, 3.0f);
  NE_TEST_ASSERT(compound.equals(Vec2(3.0f, 5.0f)), "Vec2 += operator.");
  compound *= 2.0f;
  NE_TEST_ASSERT(compound.equals(Vec2(6.0f, 10.0f)), "Vec2 *= scalar operator.");

  NE_TEST_ASSERT(math::equals((a - Vec2::Zero).length(), 5.0f), "Vec2 distance via difference length must be 5.");

  Vec2 normTarget = a;
  NE_TEST_ASSERT(normTarget.normalize(), "normalize() must return true for non-zero vector.");
  NE_TEST_ASSERT(normTarget.isNormalized(), "isNormalized() must be true after normalize().");
  NE_TEST_ASSERT(math::equals(normTarget.length(), 1.0f), "Normalized Vec2 length must be 1.");

  Vec2 zeroVec = Vec2::Zero;
  NE_TEST_ASSERT(!zeroVec.normalize(), "normalize() must return false for zero vector.");

  NE_TEST_ASSERT(a.toString() == "Vec2(x=3.000, y=4.000)", "Vec2 toString formatting check.");

  Vec2 deg(180.0f, 90.0f);
  Vec2 rad = math::radians(deg);
  NE_TEST_ASSERT(math::equals(rad.x, math::kPi), "180 deg in rad must be ~pi.");

  Vec2 degBack = math::degrees(rad);
  NE_TEST_ASSERT(degBack.equals(deg), "Degrees-radians roundtrip must match.");

  Vec2 lerpRes = math::lerp(Vec2(0.0f, 0.0f), Vec2(10.0f, 10.0f), 0.5f);
  NE_TEST_ASSERT(lerpRes.equals(Vec2(5.0f, 5.0f)), "Vec2 lerp at t=0.5 must be midpoint.");

  Vec2 clamped = math::clamp(Vec2(-5.0f, 15.0f), Vec2(0.0f, 0.0f), Vec2(10.0f, 10.0f));
  NE_TEST_ASSERT(clamped.equals(Vec2(0.0f, 10.0f)), "math::clamp on Vec2.");

  Vec2 reflected = math::reflect(Vec2(1.0f, -1.0f), Vec2(0.0f, 1.0f));
  NE_TEST_ASSERT(reflected.equals(Vec2(1.0f, 1.0f)), "math::reflect on Vec2.");
}

NE_TEST_CASE("vector", "Vec3 Operations & Member Functions") {
  Vec3 a(1.0f, 2.0f, 2.0f);
  Vec3 b(2.0f, 1.0f, 0.0f);

  NE_TEST_ASSERT(Vec3(Vec2(1.0f, 2.0f), 3.0f).equals(Vec3(1.0f, 2.0f, 3.0f)), "Vec3 composition constructor from Vec2.");

  NE_TEST_ASSERT(math::equals(a.length(), 3.0f), "Vec3 member length of (1,2,2) must be 3.");
  NE_TEST_ASSERT(math::equals(a.lengthSquared(), 9.0f), "Vec3 member lengthSquared must be 9.");
  NE_TEST_ASSERT(a.getUnsafeNormal().isNormalized(), "Vec3 getUnsafeNormal must be normalized.");
  NE_TEST_ASSERT(Vec3::Forward.cross(Vec3::Left).equals(Vec3::Up), "Forward x Left in member cross must equal Up (+Z).");
  NE_TEST_ASSERT(Vec3::Zero.getSafeNormal(math::kSmallNumber, Vec3::Up).equals(Vec3::Up), "Vec3 getSafeNormal zero fallback.");

  NE_TEST_ASSERT(math::equals(a[0], 1.0f) && math::equals(a[1], 2.0f) && math::equals(a[2], 2.0f), "Vec3 operator[] indexing.");
  NE_TEST_ASSERT(a.data() == &a.x, "Vec3 data() pointer points to x component.");

  NE_TEST_ASSERT((a + b).equals(Vec3(3.0f, 3.0f, 2.0f)), "Vec3 addition operator.");
  NE_TEST_ASSERT((a - b).equals(Vec3(-1.0f, 1.0f, 2.0f)), "Vec3 subtraction operator.");
  NE_TEST_ASSERT((a * b).equals(Vec3(2.0f, 2.0f, 0.0f)), "Vec3 element-wise multiplication operator.");
  NE_TEST_ASSERT((a * 3.0f).equals(Vec3(3.0f, 6.0f, 6.0f)), "Vec3 scalar multiplication operator.");
  NE_TEST_ASSERT((3.0f * a).equals(Vec3(3.0f, 6.0f, 6.0f)), "Vec3 free scalar multiplication operator.");
  NE_TEST_ASSERT((a + 1.0f).equals(Vec3(2.0f, 3.0f, 3.0f)), "Vec3 scalar bias addition operator.");
  NE_TEST_ASSERT((-a).equals(Vec3(-1.0f, -2.0f, -2.0f)), "Vec3 unary negation operator.");

  Vec3 compound(1.0f, 1.0f, 1.0f);
  compound += Vec3(1.0f, 2.0f, 3.0f);
  NE_TEST_ASSERT(compound.equals(Vec3(2.0f, 3.0f, 4.0f)), "Vec3 += operator.");
  compound *= 2.0f;
  NE_TEST_ASSERT(compound.equals(Vec3(4.0f, 6.0f, 8.0f)), "Vec3 *= scalar operator.");

  NE_TEST_ASSERT(math::equals((a - Vec3::Zero).length(), 3.0f), "Vec3 distance via difference length must be 3.");
  NE_TEST_ASSERT(Vec3::Forward.cross(Vec3::Left).equals(Vec3::Up), "Vec3 cross product must equal Up (+Z).");
  NE_TEST_ASSERT(math::equals(Vec3::Forward.dot(Vec3::Left), 0.0f), "Vec3 dot product must be 0.");

  Vec3 fwd = Vec3::Forward;
  Vec3 left = Vec3::Left;
  Vec3 up = fwd.cross(left);
  NE_TEST_ASSERT(up.equals(Vec3::Up), "Forward x Left in RH system must equal Up (+Z).");

  Vec3 normTarget = a;
  NE_TEST_ASSERT(normTarget.normalize(), "Vec3 normalize() must return true for non-zero vector.");
  NE_TEST_ASSERT(normTarget.isNormalized(), "Vec3 isNormalized() must be true after normalize().");

  NE_TEST_ASSERT(a.toString() == "Vec3(x=1.000, y=2.000, z=2.000)", "Vec3 toString formatting check.");

  Vec3 eulerDeg(90.0f, 45.0f, 0.0f);
  Vec3 eulerRad = math::radians(eulerDeg);
  Vec3 eulerBack = math::degrees(eulerRad);
  NE_TEST_ASSERT(eulerBack.equals(eulerDeg), "math::degrees/radians Vec3 roundtrip.");

  Vec3 refl = math::reflect(Vec3(1.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 1.0f));
  NE_TEST_ASSERT(refl.equals(Vec3(1.0f, 0.0f, 1.0f)), "math::reflect on Vec3.");
}

NE_TEST_CASE("vector", "Vec4 Operations & Member Functions") {
  Vec4 a(0.0f, 3.0f, 0.0f, 4.0f);
  Vec4 b(1.0f, 1.0f, 1.0f, 1.0f);

  NE_TEST_ASSERT(Vec4(Vec3(1.0f, 2.0f, 3.0f), 4.0f).equals(Vec4(1.0f, 2.0f, 3.0f, 4.0f)),
                 "Vec4 composition constructor from Vec3.");
  NE_TEST_ASSERT(Vec4(Vec2(1.0f, 2.0f), 3.0f, 4.0f).equals(Vec4(1.0f, 2.0f, 3.0f, 4.0f)),
                 "Vec4 composition constructor from Vec2.");

  NE_TEST_ASSERT(math::equals(a.length(), 5.0f), "Vec4 member length of (0,3,0,4) must be 5.");
  NE_TEST_ASSERT(math::equals(a.lengthSquared(), 25.0f), "Vec4 member lengthSquared must be 25.");
  NE_TEST_ASSERT(a.getUnsafeNormal().isNormalized(), "Vec4 getUnsafeNormal must be normalized.");
  NE_TEST_ASSERT(Vec4::Zero.getSafeNormal(math::kSmallNumber, Vec4::One).equals(Vec4::One), "Vec4 getSafeNormal zero fallback.");

  NE_TEST_ASSERT(math::equals(a[1], 3.0f) && math::equals(a[3], 4.0f), "Vec4 operator[] indexing.");
  NE_TEST_ASSERT(a.data() == &a.x, "Vec4 data() pointer points to x component.");

  NE_TEST_ASSERT((a + b).equals(Vec4(1.0f, 4.0f, 1.0f, 5.0f)), "Vec4 addition operator.");
  NE_TEST_ASSERT((a - b).equals(Vec4(-1.0f, 2.0f, -1.0f, 3.0f)), "Vec4 subtraction operator.");
  NE_TEST_ASSERT((a * 2.0f).equals(Vec4(0.0f, 6.0f, 0.0f, 8.0f)), "Vec4 scalar multiplication operator.");
  NE_TEST_ASSERT((2.0f * a).equals(Vec4(0.0f, 6.0f, 0.0f, 8.0f)), "Vec4 free scalar multiplication operator.");
  NE_TEST_ASSERT((a + 1.0f).equals(Vec4(1.0f, 4.0f, 1.0f, 5.0f)), "Vec4 scalar bias addition operator.");
  NE_TEST_ASSERT((-a).equals(Vec4(0.0f, -3.0f, 0.0f, -4.0f)), "Vec4 unary negation operator.");

  Vec4 compound(1.0f, 2.0f, 3.0f, 4.0f);
  compound += Vec4(1.0f, 1.0f, 1.0f, 1.0f);
  NE_TEST_ASSERT(compound.equals(Vec4(2.0f, 3.0f, 4.0f, 5.0f)), "Vec4 += operator.");
  compound *= 2.0f;
  NE_TEST_ASSERT(compound.equals(Vec4(4.0f, 6.0f, 8.0f, 10.0f)), "Vec4 *= scalar operator.");

  NE_TEST_ASSERT(math::equals(a.dot(Vec4(1.0f, 0.0f, 0.0f, 0.0f)), 0.0f), "Vec4 dot product must be 0.");
  NE_TEST_ASSERT(math::equals((Vec4(1.0f, 0.0f, 0.0f, 0.0f) - Vec4::Zero).length(), 1.0f), "Vec4 difference length must be 1.");

  Vec4 normTarget = a;
  NE_TEST_ASSERT(normTarget.normalize(), "Vec4 normalize() must return true for non-zero vector.");
  NE_TEST_ASSERT(normTarget.isNormalized(), "Vec4 isNormalized() must be true after normalize().");

  NE_TEST_ASSERT(a.toString() == "Vec4(x=0.000, y=3.000, z=0.000, w=4.000)", "Vec4 toString formatting check.");

  Vec4 d(180.0f, 180.0f, 180.0f, 180.0f);
  Vec4 r = math::radians(d);
  Vec4 dBack = math::degrees(r);
  NE_TEST_ASSERT(dBack.equals(d), "Vec4 degrees-radians roundtrip.");

  Vec4 clamped = math::clamp(Vec4(-1.0f, 5.0f, 15.0f, 20.0f), Vec4(0.0f), Vec4(10.0f));
  NE_TEST_ASSERT(clamped.equals(Vec4(0.0f, 5.0f, 10.0f, 10.0f)), "math::clamp on Vec4.");

  Vec4 lerpRes = math::lerp(Vec4(0.0f, 0.0f, 0.0f, 0.0f), Vec4(10.0f, 20.0f, 30.0f, 40.0f), 0.5f);
  NE_TEST_ASSERT(lerpRes.equals(Vec4(5.0f, 10.0f, 15.0f, 20.0f)), "Vec4 scalar lerp at t=0.5.");

  Vec4 lerpVecT = math::lerp(Vec4(0.0f, 0.0f, 0.0f, 0.0f), Vec4(10.0f, 20.0f, 30.0f, 40.0f), Vec4(0.0f, 0.25f, 0.5f, 1.0f));
  NE_TEST_ASSERT(lerpVecT.equals(Vec4(0.0f, 5.0f, 15.0f, 40.0f)), "Vec4 vector-t lerp.");
}

} // namespace ne::test

#endif
