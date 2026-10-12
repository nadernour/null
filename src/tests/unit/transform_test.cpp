#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "core/math/transform.h"
#include "components/transform_component.h"
#include "core/logger.h"

namespace ne::test {

NE_TEST_CASE("transform", "Transform Pure TRS & Basis Vector Correctness") {
  Transform t;

  NE_TEST_ASSERT(t.position.equals(Vec3::Zero), "Default position must be (0,0,0).");
  NE_TEST_ASSERT(t.scale.equals(Vec3::One), "Default scale must be (1,1,1).");
  NE_TEST_ASSERT(t.rotation.equals(Quat::Identity), "Default rotation must be Identity.");

  Transform tConstructed(Vec3(1.0f, 2.0f, 3.0f), Quat::Identity, Vec3(2.0f));
  NE_TEST_ASSERT(tConstructed.position.equals(Vec3(1.0f, 2.0f, 3.0f)), "Transform(pos, rot, scale) constructor.");

  Transform tRotFirst(Quat::Identity, Vec3(4.0f, 5.0f, 6.0f));
  NE_TEST_ASSERT(tRotFirst.position.equals(Vec3(4.0f, 5.0f, 6.0f)), "Transform(rot, pos, scale) constructor.");

  // Basis vectors in right-handed ROS REP-103 space (+X Forward, +Y Left, +Z Up)
  Vec3 fwd = t.getForward();
  Vec3 left = t.getLeft();
  Vec3 up = t.getUp();

  NE_TEST_ASSERT(fwd.equals(Vec3::Forward), "Default forward must be +X.");
  NE_TEST_ASSERT(left.equals(Vec3::Left), "Default left must be +Y.");
  NE_TEST_ASSERT(up.equals(Vec3::Up), "Default up must be +Z.");

  // Transform point (1, 0, 0) by translation (10, 20, 30) and scale 2
  t.position = Vec3(10.0f, 20.0f, 30.0f);
  t.scale = Vec3(2.0f);

  Vec3 transformedPoint = t.transformPoint(Vec3(1.0f, 0.0f, 0.0f));
  NE_TEST_ASSERT(transformedPoint.equals(Vec3(12.0f, 20.0f, 30.0f)), "Transformed point must equal (12, 20, 30).");

  // Transform direction vector (should ignore translation, only apply scale & rotation)
  Vec3 transformedVector = t.transformVector(Vec3(1.0f, 0.0f, 0.0f));
  NE_TEST_ASSERT(transformedVector.equals(Vec3(2.0f, 0.0f, 0.0f)),
                 "transformVector must ignore translation and scale direction.");
}

NE_TEST_CASE("transform", "Transform Interpolation & Inverse") {
  Transform a;
  a.position = Vec3::Zero;

  Transform b;
  b.position = Vec3(10.0f, 0.0f, 0.0f);

  Transform mid = Transform::slerp(a, b, 0.5f);
  NE_TEST_ASSERT(mid.position.equals(Vec3(5.0f, 0.0f, 0.0f)), "SLERP position at t=0.5 must be midpoint (5,0,0).");

  Transform invA = a.inverse();
  Vec3 originalPoint(3.0f, -4.0f, 5.0f);
  Vec3 roundTrip = invA.transformPoint(a.transformPoint(originalPoint));
  NE_TEST_ASSERT(roundTrip.equals(originalPoint, 1e-4f), "Transform inverse round trip must match original point.");
}

NE_TEST_CASE("transform", "Transform Hierarchical Composition (combine)") {
  // Translation and Scale combination
  Transform parent;
  parent.position = Vec3(10.0f, 0.0f, 0.0f);
  parent.scale = Vec3(2.0f, 2.0f, 2.0f);

  Transform child;
  child.position = Vec3(5.0f, 0.0f, 0.0f);
  child.scale = Vec3(0.5f, 0.5f, 0.5f);

  Transform world = Transform::combine(parent, child);

  // Child local +X is aligned with parent +X, scaled by 2, offset by 10 -> world pos (20, 0, 0)
  NE_TEST_ASSERT(world.position.equals(Vec3(20.0f, 0.0f, 0.0f), 1e-4f), "Hierarchical world position calculation.");
  NE_TEST_ASSERT(world.scale.equals(Vec3(1.0f, 1.0f, 1.0f), 1e-4f), "Hierarchical scale compounding.");

  // Full TRS hierarchy with rotation
  parent.setEulerAngles(math::radians(Vec3(30.0f, 45.0f, 60.0f)));
  child.setEulerAngles(math::radians(Vec3(15.0f, -20.0f, 10.0f)));
  Transform worldWithRot = Transform::combine(parent, child);

  // Transforming a point through child then parent matches world transform
  Vec3 localPt(1.0f, 2.0f, 3.0f);
  Vec3 ptViaHierarchy = parent.transformPoint(child.transformPoint(localPt));
  Vec3 ptViaWorld = worldWithRot.transformPoint(localPt);
  NE_TEST_ASSERT(ptViaWorld.equals(ptViaHierarchy, 1e-4f), "Transform::combine matches sequential transform evaluation.");
}

NE_TEST_CASE("transform", "TransformComponent Local Setters") {
  TransformComponent tc(Transform(Vec3(10.0f, 0.0f, 0.0f), Quat::Identity, Vec3(2.0f)));
  NE_TEST_ASSERT(tc.getLocal().position.equals(Vec3(10.0f, 0.0f, 0.0f)), "Constructor stores the local transform.");

  Quat rot = Quat::angleAxis(math::radians(90.0f), Vec3::Up);
  tc.setLocalPosition(Vec3(1.0f, 2.0f, 3.0f));
  tc.setLocalRotation(rot);
  tc.setLocalScale(Vec3(4.0f, 5.0f, 6.0f));
  NE_TEST_ASSERT(tc.getLocal().equals(Transform(Vec3(1.0f, 2.0f, 3.0f), rot, Vec3(4.0f, 5.0f, 6.0f))),
                 "Setters update the local transform.");

  tc.setLocal(Transform());
  NE_TEST_ASSERT(tc.getLocal().equals(Transform()), "setLocal replaces the whole local transform.");
  NE_TEST_ASSERT(!tc.getParent().isValid() && tc.getChildren().empty(), "A new component is a root without children.");
}

NE_TEST_CASE("transform", "Transform Rigid Inverse (inverseNoScale)") {
  Transform t;
  t.position = Vec3(3.0f, -7.5f, 12.0f);
  t.setEulerAngles(math::radians(Vec3(25.0f, -40.0f, 15.0f)));

  Transform rigidInv = t.inverseNoScale();
  NE_TEST_ASSERT(rigidInv.rotation.equals(t.rotation.conjugate()), "inverseNoScale rotation must equal rotation conjugate.");
  NE_TEST_ASSERT(rigidInv.position.equals(rigidInv.rotation * -t.position), "inverseNoScale position must match InvRot * -pos.");
  NE_TEST_ASSERT(rigidInv.scale.equals(t.scale), "inverseNoScale must preserve scale.");

  Mat4 forwardMat = t.toMatrix();
  Mat4 invMat = rigidInv.toMatrix();
  Mat4 expectedInv = forwardMat.inversed();

  NE_TEST_ASSERT(invMat.equals(expectedInv, 1e-4f), "inverseNoScale().toMatrix() must match general Mat4::inversed().");

  Mat4 identity1 = forwardMat * invMat;
  Mat4 identity2 = invMat * forwardMat;

  NE_TEST_ASSERT(identity1.equals(Mat4::Identity, 1e-4f), "M * M^-1 must equal Identity.");
  NE_TEST_ASSERT(identity2.equals(Mat4::Identity, 1e-4f), "M^-1 * M must equal Identity.");

  Vec3 pt(5.0f, -2.0f, 8.0f);
  Vec4 transformed = forwardMat * Vec4(pt.x, pt.y, pt.z, 1.0f);
  Vec4 restored = invMat * transformed;
  NE_TEST_ASSERT(Vec3(restored.x, restored.y, restored.z).equals(pt, 1e-4f),
                 "Transforming point through forward then inverse restores original.");
}

NE_TEST_CASE("transform", "Transform Memory Layout & POD Properties") {
  static_assert(sizeof(Transform) == 40, "Transform must be 40 bytes (12 + 16 + 12, zero padding).");
  static_assert(std::is_standard_layout_v<Transform>, "Transform must be standard layout.");

  NE_TEST_ASSERT(sizeof(Transform) == 40, "Transform sizeof check (40 bytes compact).");
  NE_TEST_ASSERT(std::is_standard_layout_v<Transform>, "Transform standard layout check.");
}

NE_TEST_CASE("transform", "Transform fromMatrix Decomposition") {
  // Non-uniform scale round trip
  Transform original(Vec3(3.0f, -1.0f, 2.5f), Quat::fromEuler(math::radians(Vec3(10.0f, 70.0f, -35.0f))), Vec3(2.0f, 0.5f, 3.0f));
  Transform decomposed = Transform::fromMatrix(original.toMatrix());
  NE_TEST_ASSERT(decomposed.position.equals(original.position, 1e-4f), "fromMatrix must recover translation.");
  NE_TEST_ASSERT(decomposed.scale.equals(original.scale, 1e-4f), "fromMatrix must recover non-uniform scale.");
  NE_TEST_ASSERT(decomposed.toMatrix().equals(original.toMatrix(), 1e-4f), "fromMatrix must reproduce the matrix.");

  // Mirroring matrix yields a negative X scale that reproduces it
  Transform mirrored(Vec3(1.0f, 2.0f, 3.0f), Quat::fromEuler(math::radians(Vec3(0.0f, 0.0f, 45.0f))), Vec3(1.0f, -2.0f, 1.0f));
  Transform mirroredDecomposed = Transform::fromMatrix(mirrored.toMatrix());
  NE_TEST_ASSERT(mirroredDecomposed.scale.x < 0.0f, "Mirroring matrix must decompose to a negative X scale.");
  NE_TEST_ASSERT(mirroredDecomposed.toMatrix().equals(mirrored.toMatrix(), 1e-4f),
                 "Mirrored decomposition must reproduce the matrix.");
}

} // namespace ne::test

#endif
