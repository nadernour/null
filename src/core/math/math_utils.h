#pragma once

#include "core/math/simd.h"
#include <cmath>
#include <concepts>

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

constexpr bool equals(float iA, float iB, float iTolerance = kKindaSmallNumber) {
  return abs(iA - iB) <= iTolerance;
}

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

} // namespace ne::math

