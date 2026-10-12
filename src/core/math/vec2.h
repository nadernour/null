#pragma once

#include "core/math/math_utils.h"
#include <string>

namespace ne {

struct Vec2 {
  float x{0.0f};
  float y{0.0f};

  constexpr Vec2() = default;
  constexpr explicit Vec2(float iScalar) : x(iScalar), y(iScalar) {}
  constexpr Vec2(float iX, float iY) : x(iX), y(iY) {}

  constexpr float operator[](size_t iIndex) const { return (&x)[iIndex]; }

  constexpr float& operator[](size_t iIndex) { return (&x)[iIndex]; }

  constexpr const float* data() const { return &x; }

  constexpr float* data() { return &x; }

  static const Vec2 Zero;
  static const Vec2 One;
  static const Vec2 UnitX;
  static const Vec2 UnitY;

  inline float length() const { return math::sqrt(x * x + y * y); }

  inline float lengthSquared() const { return x * x + y * y; }

  inline float size() const { return length(); }

  inline float sizeSquared() const { return lengthSquared(); }

  inline bool normalize(float iTolerance = math::kSmallNumber) {
    float lenSq = lengthSquared();
    if (lenSq > iTolerance) {
      float invLen = math::invSqrt(lenSq);
      x *= invLen;
      y *= invLen;
      return true;
    }
    x = 0.0f;
    y = 0.0f;
    return false;
  }

  inline bool isNormalized(float iTolerance = math::kKindaSmallNumber) const {
    return math::abs(lengthSquared() - 1.0f) <= iTolerance;
  }

  inline Vec2 getUnsafeNormal() const {
    float invLen = math::invSqrt(lengthSquared());
    return Vec2(x * invLen, y * invLen);
  }

  inline Vec2 getSafeNormal(float iTolerance = math::kSmallNumber, const Vec2& iFallback = Vec2::Zero) const {
    float lenSq = lengthSquared();
    if (lenSq > iTolerance) {
      float invLen = math::invSqrt(lenSq);
      return Vec2(x * invLen, y * invLen);
    }
    return iFallback;
  }

  inline std::string toString() const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Vec2(x=%.3f, y=%.3f)", x, y);
    return std::string(buf);
  }

  constexpr float dot(const Vec2& iOther) const { return x * iOther.x + y * iOther.y; }

  constexpr bool equals(const Vec2& iOther, float iTolerance = math::kKindaSmallNumber) const {
    return math::abs(x - iOther.x) <= iTolerance && math::abs(y - iOther.y) <= iTolerance;
  }

  constexpr Vec2 operator+(const Vec2& iV) const { return Vec2(x + iV.x, y + iV.y); }

  constexpr Vec2 operator-(const Vec2& iV) const { return Vec2(x - iV.x, y - iV.y); }

  constexpr Vec2 operator*(const Vec2& iV) const { return Vec2(x * iV.x, y * iV.y); }

  constexpr Vec2 operator/(const Vec2& iV) const { return Vec2(x / iV.x, y / iV.y); }

  constexpr Vec2 operator+(float iBias) const { return Vec2(x + iBias, y + iBias); }

  constexpr Vec2 operator-(float iBias) const { return Vec2(x - iBias, y - iBias); }

  constexpr Vec2 operator*(float iScale) const { return Vec2(x * iScale, y * iScale); }

  constexpr Vec2 operator/(float iScale) const {
    float inv = 1.0f / iScale;
    return Vec2(x * inv, y * inv);
  }

  constexpr Vec2 operator-() const { return Vec2(-x, -y); }

  constexpr bool operator==(const Vec2& iV) const { return x == iV.x && y == iV.y; }

  constexpr bool operator!=(const Vec2& iV) const { return !(*this == iV); }

  constexpr Vec2& operator+=(const Vec2& iV) {
    x += iV.x;
    y += iV.y;
    return *this;
  }

  constexpr Vec2& operator-=(const Vec2& iV) {
    x -= iV.x;
    y -= iV.y;
    return *this;
  }

  constexpr Vec2& operator*=(const Vec2& iV) {
    x *= iV.x;
    y *= iV.y;
    return *this;
  }

  constexpr Vec2& operator/=(const Vec2& iV) {
    x /= iV.x;
    y /= iV.y;
    return *this;
  }

  constexpr Vec2& operator+=(float iBias) {
    x += iBias;
    y += iBias;
    return *this;
  }

  constexpr Vec2& operator-=(float iBias) {
    x -= iBias;
    y -= iBias;
    return *this;
  }

  constexpr Vec2& operator*=(float iScale) {
    x *= iScale;
    y *= iScale;
    return *this;
  }

  constexpr Vec2& operator/=(float iScale) {
    float inv = 1.0f / iScale;
    x *= inv;
    y *= inv;
    return *this;
  }
};

inline const Vec2 Vec2::Zero{0.0f, 0.0f};
inline const Vec2 Vec2::One{1.0f, 1.0f};
inline const Vec2 Vec2::UnitX{1.0f, 0.0f};
inline const Vec2 Vec2::UnitY{0.0f, 1.0f};

constexpr Vec2 operator*(float iScale, const Vec2& iV) { return Vec2(iV.x * iScale, iV.y * iScale); }

constexpr Vec2 operator+(float iBias, const Vec2& iV) { return Vec2(iV.x + iBias, iV.y + iBias); }

struct IVec2 {
  int x{0};
  int y{0};

  constexpr IVec2() = default;
  constexpr explicit IVec2(int iScalar) : x(iScalar), y(iScalar) {}
  constexpr IVec2(int iX, int iY) : x(iX), y(iY) {}
};

struct UVec2 {
  unsigned int x{0};
  unsigned int y{0};

  constexpr UVec2() = default;
  constexpr explicit UVec2(unsigned int iScalar) : x(iScalar), y(iScalar) {}
  constexpr UVec2(unsigned int iX, unsigned int iY) : x(iX), y(iY) {}
};

namespace math {

constexpr Vec2 clamp(const Vec2& iV, const Vec2& iMinVal, const Vec2& iMaxVal) {
  return Vec2(math::clamp(iV.x, iMinVal.x, iMaxVal.x), math::clamp(iV.y, iMinVal.y, iMaxVal.y));
}

constexpr Vec2 min(const Vec2& iA, const Vec2& iB) { return Vec2(math::min(iA.x, iB.x), math::min(iA.y, iB.y)); }

constexpr Vec2 max(const Vec2& iA, const Vec2& iB) { return Vec2(math::max(iA.x, iB.x), math::max(iA.y, iB.y)); }

constexpr Vec2 abs(const Vec2& iV) { return Vec2(math::abs(iV.x), math::abs(iV.y)); }

constexpr Vec2 radians(const Vec2& iDeg) { return Vec2(math::radians(iDeg.x), math::radians(iDeg.y)); }

constexpr Vec2 degrees(const Vec2& iRad) { return Vec2(math::degrees(iRad.x), math::degrees(iRad.y)); }

constexpr Vec2 reflect(const Vec2& iV, const Vec2& iNormal) { return iV - iNormal * (2.0f * iV.dot(iNormal)); }

} // namespace math

} // namespace ne
