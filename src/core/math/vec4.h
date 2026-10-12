#pragma once

#include "core/math/vec3.h"
#include <string>

namespace ne {

struct Vec4 {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float w{0.0f};

  constexpr Vec4() = default;
  constexpr explicit Vec4(float iScalar) : x(iScalar), y(iScalar), z(iScalar), w(iScalar) {}
  constexpr Vec4(float iX, float iY, float iZ, float iW) : x(iX), y(iY), z(iZ), w(iW) {}
  constexpr Vec4(const Vec3& iXYZ, float iW) : x(iXYZ.x), y(iXYZ.y), z(iXYZ.z), w(iW) {}
  constexpr Vec4(const Vec2& iXY, float iZ, float iW) : x(iXY.x), y(iXY.y), z(iZ), w(iW) {}

  constexpr float operator[](size_t iIndex) const { return (&x)[iIndex]; }

  constexpr float& operator[](size_t iIndex) { return (&x)[iIndex]; }

  constexpr const float* data() const { return &x; }

  constexpr float* data() { return &x; }

  static const Vec4 Zero;
  static const Vec4 One;

  inline float length() const { return math::sqrt(x * x + y * y + z * z + w * w); }

  inline float lengthSquared() const { return x * x + y * y + z * z + w * w; }

  inline float size() const { return length(); }

  inline float sizeSquared() const { return lengthSquared(); }

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
    w = 0.0f;
    return false;
  }

  inline bool isNormalized(float iTolerance = math::kKindaSmallNumber) const {
    return math::abs(lengthSquared() - 1.0f) <= iTolerance;
  }

  inline Vec4 getUnsafeNormal() const {
    float invLen = math::invSqrt(lengthSquared());
    return Vec4(x * invLen, y * invLen, z * invLen, w * invLen);
  }

  inline Vec4 getSafeNormal(float iTolerance = math::kSmallNumber, const Vec4& iFallback = Vec4::Zero) const {
    float lenSq = lengthSquared();
    if (lenSq > iTolerance) {
      float invLen = math::invSqrt(lenSq);
      return Vec4(x * invLen, y * invLen, z * invLen, w * invLen);
    }
    return iFallback;
  }

  inline std::string toString() const {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Vec4(x=%.3f, y=%.3f, z=%.3f, w=%.3f)", x, y, z, w);
    return std::string(buf);
  }

  constexpr float dot(const Vec4& iOther) const { return x * iOther.x + y * iOther.y + z * iOther.z + w * iOther.w; }

  constexpr bool equals(const Vec4& iOther, float iTolerance = math::kKindaSmallNumber) const {
    return math::abs(x - iOther.x) <= iTolerance && math::abs(y - iOther.y) <= iTolerance &&
           math::abs(z - iOther.z) <= iTolerance && math::abs(w - iOther.w) <= iTolerance;
  }

  constexpr Vec4 operator+(const Vec4& iV) const { return Vec4(x + iV.x, y + iV.y, z + iV.z, w + iV.w); }

  constexpr Vec4 operator-(const Vec4& iV) const { return Vec4(x - iV.x, y - iV.y, z - iV.z, w - iV.w); }

  constexpr Vec4 operator*(const Vec4& iV) const { return Vec4(x * iV.x, y * iV.y, z * iV.z, w * iV.w); }

  constexpr Vec4 operator/(const Vec4& iV) const { return Vec4(x / iV.x, y / iV.y, z / iV.z, w / iV.w); }

  constexpr Vec4 operator+(float iBias) const { return Vec4(x + iBias, y + iBias, z + iBias, w + iBias); }

  constexpr Vec4 operator-(float iBias) const { return Vec4(x - iBias, y - iBias, z - iBias, w - iBias); }

  constexpr Vec4 operator*(float iScale) const { return Vec4(x * iScale, y * iScale, z * iScale, w * iScale); }

  constexpr Vec4 operator/(float iScale) const {
    float inv = 1.0f / iScale;
    return Vec4(x * inv, y * inv, z * inv, w * inv);
  }

  constexpr Vec4 operator-() const { return Vec4(-x, -y, -z, -w); }

  constexpr bool operator==(const Vec4& iV) const { return x == iV.x && y == iV.y && z == iV.z && w == iV.w; }

  constexpr bool operator!=(const Vec4& iV) const { return !(*this == iV); }

  constexpr Vec4& operator+=(const Vec4& iV) {
    x += iV.x;
    y += iV.y;
    z += iV.z;
    w += iV.w;
    return *this;
  }

  constexpr Vec4& operator-=(const Vec4& iV) {
    x -= iV.x;
    y -= iV.y;
    z -= iV.z;
    w -= iV.w;
    return *this;
  }

  constexpr Vec4& operator*=(const Vec4& iV) {
    x *= iV.x;
    y *= iV.y;
    z *= iV.z;
    w *= iV.w;
    return *this;
  }

  constexpr Vec4& operator/=(const Vec4& iV) {
    x /= iV.x;
    y /= iV.y;
    z /= iV.z;
    w /= iV.w;
    return *this;
  }

  constexpr Vec4& operator+=(float iBias) {
    x += iBias;
    y += iBias;
    z += iBias;
    w += iBias;
    return *this;
  }

  constexpr Vec4& operator-=(float iBias) {
    x -= iBias;
    y -= iBias;
    z -= iBias;
    w -= iBias;
    return *this;
  }

  constexpr Vec4& operator*=(float iScale) {
    x *= iScale;
    y *= iScale;
    z *= iScale;
    w *= iScale;
    return *this;
  }

  constexpr Vec4& operator/=(float iScale) {
    float inv = 1.0f / iScale;
    x *= inv;
    y *= inv;
    z *= inv;
    w *= inv;
    return *this;
  }
};

inline const Vec4 Vec4::Zero{0.0f, 0.0f, 0.0f, 0.0f};
inline const Vec4 Vec4::One{1.0f, 1.0f, 1.0f, 1.0f};

constexpr Vec4 operator*(float iScale, const Vec4& iV) {
  return Vec4(iV.x * iScale, iV.y * iScale, iV.z * iScale, iV.w * iScale);
}

constexpr Vec4 operator+(float iBias, const Vec4& iV) { return Vec4(iV.x + iBias, iV.y + iBias, iV.z + iBias, iV.w + iBias); }

struct IVec4 {
  int x{0};
  int y{0};
  int z{0};
  int w{0};

  constexpr IVec4() = default;
  constexpr explicit IVec4(int iScalar) : x(iScalar), y(iScalar), z(iScalar), w(iScalar) {}
  constexpr IVec4(int iX, int iY, int iZ, int iW) : x(iX), y(iY), z(iZ), w(iW) {}
};

struct UVec4 {
  unsigned int x{0};
  unsigned int y{0};
  unsigned int z{0};
  unsigned int w{0};

  constexpr UVec4() = default;
  constexpr explicit UVec4(unsigned int iScalar) : x(iScalar), y(iScalar), z(iScalar), w(iScalar) {}
  constexpr UVec4(unsigned int iX, unsigned int iY, unsigned int iZ, unsigned int iW) : x(iX), y(iY), z(iZ), w(iW) {}
};

namespace math {

constexpr Vec4 clamp(const Vec4& iV, const Vec4& iMinVal, const Vec4& iMaxVal) {
  return Vec4(math::clamp(iV.x, iMinVal.x, iMaxVal.x), math::clamp(iV.y, iMinVal.y, iMaxVal.y),
              math::clamp(iV.z, iMinVal.z, iMaxVal.z), math::clamp(iV.w, iMinVal.w, iMaxVal.w));
}

constexpr Vec4 min(const Vec4& iA, const Vec4& iB) {
  return Vec4(math::min(iA.x, iB.x), math::min(iA.y, iB.y), math::min(iA.z, iB.z), math::min(iA.w, iB.w));
}

constexpr Vec4 max(const Vec4& iA, const Vec4& iB) {
  return Vec4(math::max(iA.x, iB.x), math::max(iA.y, iB.y), math::max(iA.z, iB.z), math::max(iA.w, iB.w));
}

constexpr Vec4 abs(const Vec4& iV) { return Vec4(math::abs(iV.x), math::abs(iV.y), math::abs(iV.z), math::abs(iV.w)); }

constexpr Vec4 radians(const Vec4& iDeg) {
  return Vec4(math::radians(iDeg.x), math::radians(iDeg.y), math::radians(iDeg.z), math::radians(iDeg.w));
}

constexpr Vec4 degrees(const Vec4& iRad) {
  return Vec4(math::degrees(iRad.x), math::degrees(iRad.y), math::degrees(iRad.z), math::degrees(iRad.w));
}

} // namespace math

} // namespace ne
