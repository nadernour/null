#pragma once

#include <array>
#include <cstdint>

namespace ne {

// 8-bit RGBA as stored in images. Color textures store RGB sRGB-encoded; data textures (normals, roughness) store it raw.
struct Color {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
};

// Float RGBA in linear light, where averaging and blending are physically correct
struct LinearColor {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;

  static const LinearColor White;

  // Linear value of each 8-bit sRGB value. A literal table, so it is ready even during static initialization.
  static const std::array<float, 256> kSrgbToLinear;

  // RGB through the sRGB curve; alpha is never sRGB-encoded
  static LinearColor fromSrgb(Color iColor) {
    return {kSrgbToLinear[iColor.r], kSrgbToLinear[iColor.g], kSrgbToLinear[iColor.b], iColor.a * (1.0f / 255.0f)};
  }
  Color toSrgb() const;

  // Every channel as stored, scaled to [0, 1]
  static LinearColor fromUnorm(Color iColor) {
    constexpr float kScale = 1.0f / 255.0f;
    return {iColor.r * kScale, iColor.g * kScale, iColor.b * kScale, iColor.a * kScale};
  }
  Color toUnorm() const;

  LinearColor operator*(float iScale) const { return {r * iScale, g * iScale, b * iScale, a * iScale}; }

  LinearColor& operator+=(const LinearColor& iOther) {
    r += iOther.r;
    g += iOther.g;
    b += iOther.b;
    a += iOther.a;
    return *this;
  }
};

inline const LinearColor LinearColor::White{1.0f, 1.0f, 1.0f, 1.0f};

} // namespace ne
