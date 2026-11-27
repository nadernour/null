#pragma once

#include "core/math/simd.h"
#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>

namespace ne::math {

constexpr float kPi = 3.1415926535897932f;
constexpr float kInvPi = 0.31830988618f;
constexpr float kHalfPi = 1.57079632679f;

constexpr float kSmallNumber = 1.e-8f;
constexpr float kKindaSmallNumber = 1.e-4f;
constexpr float kBigNumber = 3.4e+38f;
constexpr float kGoldenRatio = 1.6180339887498948482045868343656381f;

constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

template <typename T>
constexpr T clamp(T iVal, T iMinVal, T iMaxVal) {
  return (iVal < iMinVal) ? iMinVal : ((iVal > iMaxVal) ? iMaxVal : iVal);
}

template <typename T, typename U = float>
constexpr T lerp(T iA, T iB, U iT) {
  return static_cast<T>(iA + (iB - iA) * iT);
}

template <typename T>
constexpr T min(T iA, T iB) {
  return (iA < iB) ? iA : iB;
}

template <typename T>
constexpr T max(T iA, T iB) {
  return (iA > iB) ? iA : iB;
}

template <typename T>
constexpr T abs(T iVal) {
  return (iVal < T(0)) ? -iVal : iVal;
}

template <typename T>
constexpr T radians(T iDeg) {
  return iDeg * static_cast<T>(kDegToRad);
}

template <typename T>
constexpr T degrees(T iRad) {
  return iRad * static_cast<T>(kRadToDeg);
}

constexpr bool equals(float iA, float iB, float iTolerance = kKindaSmallNumber) { return abs(iA - iB) <= iTolerance; }

constexpr bool equals(double iA, double iB, double iTolerance = static_cast<double>(kKindaSmallNumber)) {
  return abs(iA - iB) <= iTolerance;
}

// Scalar math goes through here so a deterministic implementation can replace std in one place
template <std::floating_point T>
T sqrt(T iVal) {
  return std::sqrt(iVal);
}

template <std::floating_point T>
T pow(T iBase, T iExp) {
  return std::pow(iBase, iExp);
}

template <std::floating_point T>
T log2(T iVal) {
  return std::log2(iVal);
}

template <std::floating_point T>
T exp2(T iVal) {
  return std::exp2(iVal);
}

// Nearest whole number, halves away from zero
template <std::floating_point T>
T round(T iVal) {
  return std::round(iVal);
}

// Nearest integer, halves away from zero
template <std::floating_point T>
int64_t roundToInt64(T iVal) {
  return static_cast<int64_t>(std::llround(iVal));
}

template <std::floating_point T>
T sin(T iRad) {
  return std::sin(iRad);
}

template <std::floating_point T>
T cos(T iRad) {
  return std::cos(iRad);
}

template <std::floating_point T>
T tan(T iRad) {
  return std::tan(iRad);
}

template <std::floating_point T>
T asin(T iVal) {
  return std::asin(iVal);
}

template <std::floating_point T>
T acos(T iVal) {
  return std::acos(iVal);
}

template <std::floating_point T>
T atan2(T iY, T iX) {
  return std::atan2(iY, iX);
}

// IEEE 754 binary16 bits of iValue, rounded to nearest even; past 65504 gives infinity, NaN stays NaN. Fabian Giesen's
// public-domain float_to_half_fast3_rtne (https://gist.github.com/rygorous/2156668), with std::bit_cast for its unions.
constexpr uint16_t floatToHalf(float iValue) {
  constexpr uint32_t kFloatInfinity = 255u << 23;
  constexpr uint32_t kHalfOverflow = (127u + 16u) << 23; // 2^16: every float from here on is infinity in binary16
  constexpr uint32_t kSmallestNormal = 113u << 23;       // 2^-14
  // 0.5 with its lowest mantissa bit at binary16's smallest subnormal, so adding it rounds a subnormal into place
  constexpr uint32_t kSubnormalMagic = ((127u - 15u) + (23u - 10u) + 1u) << 23;

  uint32_t bits = std::bit_cast<uint32_t>(iValue);
  const uint32_t sign = bits & 0x80000000u;
  bits ^= sign;

  uint32_t half = 0;
  if (bits >= kHalfOverflow) {
    half = bits > kFloatInfinity ? 0x7E00u : 0x7C00u;
  } else if (bits < kSmallestNormal) {
    const float aligned = std::bit_cast<float>(bits) + std::bit_cast<float>(kSubnormalMagic);
    half = std::bit_cast<uint32_t>(aligned) - kSubnormalMagic;
  } else {
    const uint32_t mantissaOdd = (bits >> 13) & 1u;
    bits += ((15u - 127u) << 23) + 0xFFFu; // Rebias the exponent and add just under half a binary16 ulp...
    bits += mantissaOdd;                   // ...or exactly half when that rounds to even
    half = bits >> 13;
  }
  return static_cast<uint16_t>(half | (sign >> 16));
}

} // namespace ne::math
