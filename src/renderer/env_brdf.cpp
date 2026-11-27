#include "renderer/env_brdf.h"
#include "core/math/math_utils.h"
#include "core/math/vec3.h"

#include <cstring>

// The integration is ported from Google Filament's libs/ibl (CubemapIBL.cpp's DFV and its helpers, and utilities.h's
// hammersley), Copyright (C) 2015 The Android Open Source Project, Apache License 2.0
namespace ne {

namespace {

constexpr uint32_t kTableSize = 64;
constexpr uint32_t kSampleCount = 256;

float pow5(float iX) {
  const float x2 = iX * iX;
  return x2 * x2 * iX;
}

// (i / n, i's bits mirrored about the binary point), with iN = 1 / n
Vec2 hammersley(uint32_t iIndex, float iN) {
  uint32_t bits = iIndex;
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return Vec2(static_cast<float>(iIndex) * iN, static_cast<float>(bits) * (0.5f / 0x80000000U));
}

// A half vector drawn with pdf D(a) * cosTheta, around N = +Z; a is GGX's alpha
Vec3 hemisphereImportanceSampleDggx(Vec2 iU, float iA) {
  const float phi = 2.0f * math::kPi * iU.x;
  // (a + 1)(a - 1) rather than a^2 - 1, for floating-point accuracy
  const float cosTheta2 = (1.0f - iU.y) / (1.0f + (iA + 1.0f) * ((iA - 1.0f) * iU.y));
  const float cosTheta = math::sqrt(cosTheta2);
  const float sinTheta = math::sqrt(1.0f - cosTheta2);
  return Vec3(sinTheta * math::cos(phi), sinTheta * math::sin(phi), cosTheta);
}

// Height-correlated Smith G / (4 NoL NoV) (Heitz 2014), the light loop's visibility
float visibility(float iNoV, float iNoL, float iA) {
  const float a2 = iA * iA;
  const float GGXL = iNoV * math::sqrt((iNoL - iNoL * a2) * iNoL + a2);
  const float GGXV = iNoL * math::sqrt((iNoV - iNoV * a2) * iNoV + a2);
  return 0.5f / (GGXV + GGXL);
}

float saturate(float iValue) { return math::clamp(iValue, 0.0f, 1.0f); }

// Filament's DFV, under its names: (A, B) for one view angle and GGX alpha, from iNumSamples GGX importance samples
Vec2 dfv(float iNoV, float iLinearRoughness, uint32_t iNumSamples) {
  const float NoV = iNoV;
  const float linearRoughness = iLinearRoughness;
  Vec2 r(0.0f);
  const Vec3 V(math::sqrt(1.0f - NoV * NoV), 0.0f, NoV);
  for (uint32_t i = 0; i < iNumSamples; i++) {
    const Vec2 u = hammersley(i, 1.0f / static_cast<float>(iNumSamples));
    const Vec3 H = hemisphereImportanceSampleDggx(u, linearRoughness);
    const Vec3 L = H * (2.0f * V.dot(H)) - V;
    const float VoH = saturate(V.dot(H));
    const float NoL = saturate(L.z);
    const float NoH = saturate(H.z);
    if (NoL > 0.0f) {
      // Fresnel is f0 * (1 - Fc) + f90 * Fc, so the sum splits into a scale of f0 (x) and of f90 (y)
      const float v = visibility(NoV, NoL, linearRoughness) * NoL * (VoH / NoH);
      const float Fc = pow5(1.0f - VoH);
      r.x += v * (1.0f - Fc);
      r.y += v * Fc;
    }
  }
  return r * (4.0f / static_cast<float>(iNumSamples));
}

} // namespace

ImageData buildBrdfTable() {
  ImageData table{
      .width = kTableSize,
      .height = kTableSize,
      .channels = 2,
      .channelType = ImageData::ChannelType::Float16,
      .srgb = false,
  };
  table.pixels.resize(table.getSizeInBytes());
  uint8_t* texel = table.pixels.data();
  for (uint32_t y = 0; y < kTableSize; ++y) {
    const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(kTableSize);
    for (uint32_t x = 0; x < kTableSize; ++x) {
      const float NdotV = (static_cast<float>(x) + 0.5f) / static_cast<float>(kTableSize);
      const Vec2 ab = dfv(NdotV, roughness * roughness, kSampleCount);
      const uint16_t halves[2] = {math::floatToHalf(ab.x), math::floatToHalf(ab.y)};
      std::memcpy(texel, halves, sizeof(halves));
      texel += sizeof(halves);
    }
  }
  return table;
}

} // namespace ne
