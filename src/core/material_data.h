#pragma once

#include "core/math/math.h"
#include <cstdint>
#include <string>

namespace ne {

enum class AlphaMode : uint8_t { Opaque, Mask, Blend };
enum class TextureWrap : uint8_t { Repeat, Clamp, Mirror, Count };
enum class TextureFilter : uint8_t { Linear, Nearest, Count };

// A material's texture and its sampler settings; image indexes ModelData::images, -1 for none
struct TextureRef {
  int32_t image = -1;
  TextureWrap wrapU = TextureWrap::Repeat; // glTF wrapS
  TextureWrap wrapV = TextureWrap::Repeat; // glTF wrapT
  TextureFilter filter = TextureFilter::Linear;
};

// glTF metal-roughness; each factor multiplies its texture. The defaults are the engine's own: a dielectric.
struct MaterialData {
  std::string name;
  LinearColor baseColor = LinearColor::White;
  float metallic = 0.0f;
  float roughness = 0.5f;
  Vec3 emissive{0.0f}; // As authored (factor x emissive strength), without a physical unit yet
  float normalScale = 1.0f;
  float occlusionStrength = 1.0f;
  float alphaCutoff = 0.5f;
  AlphaMode alphaMode = AlphaMode::Opaque;
  bool doubleSided = false;
  TextureRef baseColorTexture{};         // sRGB
  TextureRef metallicRoughnessTexture{}; // Linear; G = roughness, B = metallic
  TextureRef normalTexture{};            // Linear
  TextureRef occlusionTexture{};         // Linear; R
  TextureRef emissiveTexture{};          // sRGB
};

} // namespace ne
