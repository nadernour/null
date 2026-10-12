#pragma once

#include "core/math/vec2.h"
#include <string>

namespace ne {

struct Vec3 {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};

  constexpr Vec3() = default;
  constexpr explicit Vec3(float iScalar) : x(iScalar), y(iScalar), z(iScalar) {}
  constexpr Vec3(float iX, float iY, float iZ) : x(iX), y(iY), z(iZ) {}
  constexpr Vec3(const Vec2& iXY, float iZ) : x(iXY.x), y(iXY.y), z(iZ) {}

  constexpr float operator[](size_t iIndex) const { return (&x)[iIndex]; }

  constexpr float& operator[](size_t iIndex) { return (&x)[iIndex]; }

  constexpr const float* data() const { return &x; }

  constexpr float* data() { return &x; }

  // World basis: +X forward, +Y left, +Z up
  static const Vec3 Forward;
  static const Vec3 Left;
  static const Vec3 Up;
  static const Vec3 Zero;
  static const Vec3 One;

  inline float length() const { return math::sqrt(x * x + y * y + z * z); }

  inline float lengthSquared() const { return x * x + y * y + z * z; }

  inline float size() const { return length(); }

  inline float sizeSquared() const { return lengthSquared(); }

  inline bool normalize(float iTolerance = math::kSmallNumber) {
    float lenSq = lengthSquared();
    if (lenSq > iTolerance) {
      float invLen = math::invSqrt(lenSq);
      x *= invLen;
      y *= invLen;
      z *= invLen;
      return true;
    }
    x = 0.0f;
    y = 0.0f;
    z = 0.0f;
    return false;
  }

  inline bool isNormalized(float iTolerance = math::kKindaSmallNumber) const {
    return math::abs(lengthSquared() - 1.0f) <= iTolerance;
  }

  inline Vec3 getUnsafeNormal() const {
    float invLen = math::invSqrt(lengthSquared());
    return Vec3(x * invLen, y * invLen, z * invLen);
  }

  inline Vec3 getSafeNormal(float iTolerance = math::kSmallNumber, const Vec3& iFallback = Vec3::Zero) const {
    float lenSq = lengthSquared();
    if (lenSq > iTolerance) {
      float invLen = math::invSqrt(lenSq);
      return Vec3(x * invLen, y * invLen, z * invLen);
    }
    return iFallback;
  }

  inline std::string toString() const {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Vec3(x=%.3f, y=%.3f, z=%.3f)", x, y, z);
    return std::string(buf);
  }

  constexpr float dot(const Vec3& iOther) const { return x * iOther.x + y * iOther.y + z * iOther.z; }

  constexpr bool equals(const Vec3& iOther, float iTolerance = math::kKindaSmallNumber) const {
    return math::abs(x - iOther.x) <= iTolerance && math::abs(y - iOther.y) <= iTolerance &&
           math::abs(z - iOther.z) <= iTolerance;
  }

  constexpr Vec3 cross(const Vec3& iOther) const {
    return Vec3(y * iOther.z - z * iOther.y, z * iOther.x - x * iOther.z, x * iOther.y - y * iOther.x);
  }

  constexpr Vec3 operator+(const Vec3& iV) const { return Vec3(x + iV.x, y + iV.y, z + iV.z); }

  constexpr Vec3 operator-(const Vec3& iV) const { return Vec3(x - iV.x, y - iV.y, z - iV.z); }

  constexpr Vec3 operator*(const Vec3& iV) const { return Vec3(x * iV.x, y * iV.y, z * iV.z); }

  constexpr Vec3 operator/(const Vec3& iV) const { return Vec3(x / iV.x, y / iV.y, z / iV.z); }

  constexpr Vec3 operator+(float iBias) const { return Vec3(x + iBias, y + iBias, z + iBias); }

  constexpr Vec3 operator-(float iBias) const { return Vec3(x - iBias, y - iBias, z - iBias); }

  constexpr Vec3 operator*(float iScale) const { return Vec3(x * iScale, y * iScale, z * iScale); }

  constexpr Vec3 operator/(float iScale) const {
    float inv = 1.0f / iScale;
    return Vec3(x * inv, y * inv, z * inv);
  }

  constexpr Vec3 operator-() const { return Vec3(-x, -y, -z); }

  constexpr bool operator==(const Vec3& iV) const { return x == iV.x && y == iV.y && z == iV.z; }

  constexpr bool operator!=(const Vec3& iV) const { return !(*this == iV); }

  constexpr Vec3& operator+=(const Vec3& iV) {
    x += iV.x;
    y += iV.y;
    z += iV.z;
    return *this;
  }

  constexpr Vec3& operator-=(const Vec3& iV) {
    x -= iV.x;
    y -= iV.y;
    z -= iV.z;
    return *this;
  }

  constexpr Vec3& operator*=(const Vec3& iV) {
    x *= iV.x;
    y *= iV.y;
    z *= iV.z;
    return *this;
  }

  constexpr Vec3& operator/=(const Vec3& iV) {
    x /= iV.x;
    y /= iV.y;
    z /= iV.z;
    return *this;
  }

  constexpr Vec3& operator+=(float iBias) {
    x += iBias;
    y += iBias;
    z += iBias;
    return *this;
  }

  constexpr Vec3& operator-=(float iBias) {
    x -= iBias;
    y -= iBias;
    z -= iBias;
    return *this;
  }

  constexpr Vec3& operator*=(float iScale) {
    x *= iScale;
    y *= iScale;
    z *= iScale;
    return *this;
  }

  constexpr Vec3& operator/=(float iScale) {
    float inv = 1.0f / iScale;
    x *= inv;
    y *= inv;
    z *= inv;
    return *this;
  }
};

inline const Vec3 Vec3::Forward{1.0f, 0.0f, 0.0f};
inline const Vec3 Vec3::Left{0.0f, 1.0f, 0.0f};
inline const Vec3 Vec3::Up{0.0f, 0.0f, 1.0f};
inline const Vec3 Vec3::Zero{0.0f, 0.0f, 0.0f};
inline const Vec3 Vec3::One{1.0f, 1.0f, 1.0f};

constexpr Vec3 operator*(float iScale, const Vec3& iV) { return Vec3(iV.x * iScale, iV.y * iScale, iV.z * iScale); }

constexpr Vec3 operator+(float iBias, const Vec3& iV) { return Vec3(iV.x + iBias, iV.y + iBias, iV.z + iBias); }

struct IVec3 {
  int x{0};
  int y{0};
  int z{0};

  constexpr IVec3() = default;
  constexpr explicit IVec3(int iScalar) : x(iScalar), y(iScalar), z(iScalar) {}
  constexpr IVec3(int iX, int iY, int iZ) : x(iX), y(iY), z(iZ) {}
};

struct UVec3 {
  unsigned int x{0};
  unsigned int y{0};
  unsigned int z{0};

  constexpr UVec3() = default;
  constexpr explicit UVec3(unsigned int iScalar) : x(iScalar), y(iScalar), z(iScalar) {}
  constexpr UVec3(unsigned int iX, unsigned int iY, unsigned int iZ) : x(iX), y(iY), z(iZ) {}
};

namespace math {

constexpr Vec3 clamp(const Vec3& iV, const Vec3& iMinVal, const Vec3& iMaxVal) {
  return Vec3(math::clamp(iV.x, iMinVal.x, iMaxVal.x), math::clamp(iV.y, iMinVal.y, iMaxVal.y),
              math::clamp(iV.z, iMinVal.z, iMaxVal.z));
}

constexpr Vec3 min(const Vec3& iA, const Vec3& iB) {
  return Vec3(math::min(iA.x, iB.x), math::min(iA.y, iB.y), math::min(iA.z, iB.z));
}

constexpr Vec3 max(const Vec3& iA, const Vec3& iB) {
  return Vec3(math::max(iA.x, iB.x), math::max(iA.y, iB.y), math::max(iA.z, iB.z));
}

constexpr Vec3 abs(const Vec3& iV) { return Vec3(math::abs(iV.x), math::abs(iV.y), math::abs(iV.z)); }

constexpr Vec3 radians(const Vec3& iDeg) { return Vec3(math::radians(iDeg.x), math::radians(iDeg.y), math::radians(iDeg.z)); }

constexpr Vec3 degrees(const Vec3& iRad) { return Vec3(math::degrees(iRad.x), math::degrees(iRad.y), math::degrees(iRad.z)); }

constexpr Vec3 reflect(const Vec3& iV, const Vec3& iNormal) { return iV - iNormal * (2.0f * iV.dot(iNormal)); }

inline Vec3 refract(const Vec3& iV, const Vec3& iNormal, float iEta) {
  float dotVal = iV.dot(iNormal);
  float k = 1.0f - iEta * iEta * (1.0f - dotVal * dotVal);
  if (k < 0.0f) {
    return Vec3::Zero;
  }
  return iV * iEta - iNormal * (iEta * dotVal + math::sqrt(k));
}

} // namespace math

} // namespace ne
