#pragma once

#include "core/math/math.h"
#include <string>

namespace ne {

struct Transform {
  Vec3 position{Vec3::Zero};
  Quat rotation{Quat::Identity};
  Vec3 scale{Vec3::One};

  constexpr Transform() = default;
  constexpr Transform(const Vec3& iPosition, const Quat& iRotation = Quat::Identity, const Vec3& iScale = Vec3::One)
      : position(iPosition), rotation(iRotation), scale(iScale) {}
  constexpr Transform(const Quat& iRotation, const Vec3& iPosition, const Vec3& iScale = Vec3::One)
      : position(iPosition), rotation(iRotation), scale(iScale) {}

  // T * R * S
  constexpr Mat4 toMatrix() const {
    Mat4 res = rotation.toMatrix();
    res.cols[0] = res.cols[0] * scale.x;
    res.cols[1] = res.cols[1] * scale.y;
    res.cols[2] = res.cols[2] * scale.z;
    res.cols[3] = Vec4(position, 1.0f);
    return res;
  }

  // Decomposes an affine matrix without shear. A mirroring matrix yields a negative X scale;
  // a zero-scale axis yields Identity rotation.
  static Transform fromMatrix(const Mat4& iMatrix) {
    const Vec3 axisX(iMatrix[0].x, iMatrix[0].y, iMatrix[0].z);
    const Vec3 axisY(iMatrix[1].x, iMatrix[1].y, iMatrix[1].z);
    const Vec3 axisZ(iMatrix[2].x, iMatrix[2].y, iMatrix[2].z);
    const Vec3 position(iMatrix[3].x, iMatrix[3].y, iMatrix[3].z);

    Vec3 scale(axisX.length(), axisY.length(), axisZ.length());
    if (scale.x < math::kSmallNumber || scale.y < math::kSmallNumber || scale.z < math::kSmallNumber) {
      return Transform(position, Quat::Identity, scale);
    }
    if (axisX.cross(axisY).dot(axisZ) < 0.0f) {
      scale.x = -scale.x;
    }

    const Mat4 rotation(Vec4(axisX / scale.x, 0.0f), Vec4(axisY / scale.y, 0.0f), Vec4(axisZ / scale.z, 0.0f),
                        Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    return Transform(position, Quat::fromRotationMatrix(rotation), scale);
  }

  Transform inverseNoScale() const {
    Quat invRotation = rotation.conjugate();
    Vec3 invTranslation = invRotation * -position;
    return Transform(invRotation, invTranslation, scale);
  }

  Vec3 getForward() const { return rotation * Vec3::Forward; }

  Vec3 getLeft() const { return rotation * Vec3::Left; }

  Vec3 getUp() const { return rotation * Vec3::Up; }

  std::string toString() const {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "Transform(Pos: %s, Rot: %s, Scale: %s)",
      position.toString().c_str(), rotation.toString().c_str(), scale.toString().c_str());
    return std::string(buf);
  }

  Vec3 transformPoint(const Vec3& iPoint) const { return position + (rotation * (scale * iPoint)); }

  Vec3 transformVector(const Vec3& iVector) const { return rotation * (scale * iVector); }

  Transform inverse() const {
    Quat invRotation = rotation.conjugate();
    Vec3 invScale = Vec3::One / scale;
    Vec3 invPosition = invRotation * (-position * invScale);
    return Transform(invRotation, invPosition, invScale);
  }

  // Roll (X), pitch (Y), yaw (Z) in radians
  void setEulerAngles(const Vec3& iEulerRadians) { rotation = Quat::fromEuler(iEulerRadians); }

  Vec3 getEulerAngles() const { return rotation.toEuler(); }

  static Transform slerp(const Transform& iA, const Transform& iB, float iT) {
    Transform result;
    result.position = math::lerp(iA.position, iB.position, iT);
    result.rotation = Quat::slerp(iA.rotation, iB.rotation, iT);
    result.scale = math::lerp(iA.scale, iB.scale, iT);
    return result;
  }

  static Transform combine(const Transform& iParent, const Transform& iChild) {
    Transform world;
    world.position = iParent.transformPoint(iChild.position);
    world.rotation = iParent.rotation * iChild.rotation;
    world.scale = iParent.scale * iChild.scale;
    return world;
  }

  inline bool equals(const Transform& iOther, float iTolerance = math::kKindaSmallNumber) const {
    return position.equals(iOther.position, iTolerance) && rotation.equals(iOther.rotation, iTolerance) &&
           scale.equals(iOther.scale, iTolerance);
  }
};

} // namespace ne
