#pragma once

#include "core/math/math_utils.h"
#include "core/math/vec3.h"
#include "core/math/mat4.h"
#include <string>

namespace ne {

// Memory layout is (x, y, z, w), matching Slang and glTF
struct Quat {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float w{1.0f};

  constexpr Quat() = default;
  constexpr Quat(float iX, float iY, float iZ, float iW) : x(iX), y(iY), z(iZ), w(iW) {}

  static const Quat Identity;

  constexpr Vec3 operator*(const Vec3& iV) const {
    const Vec3 qv(x, y, z);
    const Vec3 uv = qv.cross(iV);
    const Vec3 uuv = qv.cross(uv);
    return iV + ((uv * w) + uuv) * 2.0f;
  }

  // Hamilton product: the result applies iQ first, then this rotation
  constexpr Quat operator*(const Quat& iQ) const {
    return Quat(
      w * iQ.x + x * iQ.w + y * iQ.z - z * iQ.y,
      w * iQ.y - x * iQ.z + y * iQ.w + z * iQ.x,
      w * iQ.z + x * iQ.y - y * iQ.x + z * iQ.w,
      w * iQ.w - x * iQ.x - y * iQ.y - z * iQ.z
    );
  }

  static inline Quat angleAxis(float iAngleRad, const Vec3& iAxis) {
    float lenSq = iAxis.lengthSquared();
    if (lenSq < math::kSmallNumber) {
      return Identity;
    }
    float invLen = math::invSqrt(lenSq);
    float halfAngle = iAngleRad * 0.5f;
    float s = math::sin(halfAngle);
    return Quat(iAxis.x * invLen * s, iAxis.y * invLen * s, iAxis.z * invLen * s, math::cos(halfAngle));
  }

  // Roll (X), pitch (Y), yaw (Z) in radians, composed as Rz * Ry * Rx (ROS RPY)
  static inline Quat fromEuler(const Vec3& iEulerRadians) {
    float radX = iEulerRadians.x * 0.5f;
    float radY = iEulerRadians.y * 0.5f;
    float radZ = iEulerRadians.z * 0.5f;

    float cx = math::cos(radX);
    float sx = math::sin(radX);
    float cy = math::cos(radY);
    float sy = math::sin(radY);
    float cz = math::cos(radZ);
    float sz = math::sin(radZ);

    return Quat(
      sx * cy * cz - cx * sy * sz,
      cx * sy * cz + sx * cy * sz,
      cx * cy * sz - sx * sy * cz,
      cx * cy * cz + sx * sy * sz
    );
  }

  // The upper 3x3 must be a pure rotation (orthonormal, no scale)
  static inline Quat fromRotationMatrix(const Mat4& iRotation) {
    // Rij = row i, column j (Mat4 is column-major)
    const float r00 = iRotation[0].x, r01 = iRotation[1].x, r02 = iRotation[2].x;
    const float r10 = iRotation[0].y, r11 = iRotation[1].y, r12 = iRotation[2].y;
    const float r20 = iRotation[0].z, r21 = iRotation[1].z, r22 = iRotation[2].z;

    // Branch on the largest of (w, x, y, z) to keep the square root argument well away from zero
    const float trace = r00 + r11 + r22;
    if (trace > 0.0f) {
      const float s = math::sqrt(trace + 1.0f) * 2.0f;
      return Quat((r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25f * s);
    }
    if (r00 > r11 && r00 > r22) {
      const float s = math::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
      return Quat(0.25f * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s);
    }
    if (r11 > r22) {
      const float s = math::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
      return Quat((r01 + r10) / s, 0.25f * s, (r12 + r21) / s, (r02 - r20) / s);
    }
    const float s = math::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
    return Quat((r02 + r20) / s, (r12 + r21) / s, 0.25f * s, (r10 - r01) / s);
  }

  static inline Quat slerp(const Quat& iA, const Quat& iB, float iT) {
    Quat qb = iB;
    float cosTheta = iA.dot(iB);

    // Take shortest path across hypersphere
    if (cosTheta < 0.0f) {
      qb = Quat(-iB.x, -iB.y, -iB.z, -iB.w);
      cosTheta = -cosTheta;
    }

    // If quaternions are almost collinear, use linear interpolation to avoid divide by zero
    if (cosTheta > 0.9995f) {
      Quat result(
        math::lerp(iA.x, qb.x, iT),
        math::lerp(iA.y, qb.y, iT),
        math::lerp(iA.z, qb.z, iT),
        math::lerp(iA.w, qb.w, iT)
      );
      result.normalize();
      return result;
    }

    float theta = math::acos(math::clamp(cosTheta, -1.0f, 1.0f));
    float sinTheta = math::sin(theta);
    float scale0 = math::sin((1.0f - iT) * theta) / sinTheta;
    float scale1 = math::sin(iT * theta) / sinTheta;

    return Quat(
      scale0 * iA.x + scale1 * qb.x,
      scale0 * iA.y + scale1 * qb.y,
      scale0 * iA.z + scale1 * qb.z,
      scale0 * iA.w + scale1 * qb.w
    );
  }

  constexpr float dot(const Quat& iOther) const { return x * iOther.x + y * iOther.y + z * iOther.z + w * iOther.w; }

  constexpr float lengthSquared() const { return x * x + y * y + z * z + w * w; }

  inline float length() const { return math::sqrt(lengthSquared()); }

  constexpr Mat4 toMatrix() const {
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    Mat4 res(1.0f);
    res.cols[0] = Vec4(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy),        0.0f);
    res.cols[1] = Vec4(2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),        0.0f);
    res.cols[2] = Vec4(2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy), 0.0f);
    res.cols[3] = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return res;
  }

  // Equals the inverse for unit quaternions
  constexpr Quat conjugate() const { return Quat(-x, -y, -z, w); }

  inline Quat inverse() const {
    float lenSq = lengthSquared();
    if (lenSq > math::kSmallNumber) {
      float invLenSq = 1.0f / lenSq;
      return Quat(-x * invLenSq, -y * invLenSq, -z * invLenSq, w * invLenSq);
    }
    return Identity;
  }

  // Inverse of fromEuler()
  inline Vec3 toEuler() const {
    float rollY = 2.0f * (y * z + w * x);
    float rollX = w * w - x * x - y * y + z * z;
    float rollRad = 0.0f;
    if (math::abs(rollX) < math::kSmallNumber && math::abs(rollY) < math::kSmallNumber) {
      rollRad = 2.0f * math::atan2(x, w);
    } else {
      rollRad = math::atan2(rollY, rollX);
    }

    float sinPitch = math::clamp(-2.0f * (x * z - w * y), -1.0f, 1.0f);
    float pitchRad = math::asin(sinPitch);

    float yawY = 2.0f * (x * y + w * z);
    float yawX = w * w + x * x - y * y - z * z;
    float yawRad = 0.0f;
    if (math::abs(yawX) < math::kSmallNumber && math::abs(yawY) < math::kSmallNumber) {
      yawRad = 0.0f;
    } else {
      yawRad = math::atan2(yawY, yawX);
    }

    return Vec3(rollRad, pitchRad, yawRad);
  }

  // Resets to Identity when the length is near zero
  inline bool normalize(float iTolerance = math::kSmallNumber) {
    float lenSq = lengthSquared();
    if (lenSq > iTolerance) {
      float invLen = math::invSqrt(lenSq);
      x *= invLen;
      y *= invLen;
      z *= invLen;
      w *= invLen;
      return true;
    }
    x = 0.0f;
    y = 0.0f;
    z = 0.0f;
    w = 1.0f;
    return false;
  }

  inline bool equals(const Quat& iOther, float iTolerance = math::kKindaSmallNumber) const {
    return math::abs(x - iOther.x) <= iTolerance && math::abs(y - iOther.y) <= iTolerance &&
           math::abs(z - iOther.z) <= iTolerance && math::abs(w - iOther.w) <= iTolerance;
  }

  inline std::string toString() const {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Quat(x=%.3f, y=%.3f, z=%.3f, w=%.3f)", x, y, z, w);
    return std::string(buf);
  }
};

inline const Quat Quat::Identity{0.0f, 0.0f, 0.0f, 1.0f};

} // namespace ne
