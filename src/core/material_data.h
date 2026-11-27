#pragma once

#include "core/math/math.h"
#include <cstdint>
#include <string>

namespace ne {

enum class AlphaMode : uint8_t { Opaque, Mask, Blend, Count };
enum class TextureWrap : uint8_t { Repeat, Clamp, Mirror, Count };
enum class TextureFilter : uint8_t { Linear, Nearest, Count };

// Names for logs and debug names. The switches have no default, so -Wswitch flags an enumerator without a name.
constexpr const char* toString(AlphaMode iAlphaMode) {
  switch (iAlphaMode) {
    case AlphaMode::Opaque:
      return "Opaque";
    case AlphaMode::Mask:
      return "Mask";
    case AlphaMode::Blend:
      return "Blend";
    case AlphaMode::Count:
      break;
  }
  return "Unknown";
}

constexpr const char* toString(TextureWrap iWrap) {
  switch (iWrap) {
    case TextureWrap::Repeat:
      return "Repeat";
    case TextureWrap::Clamp:
      return "Clamp";
    case TextureWrap::Mirror:
      return "Mirror";
    case TextureWrap::Count:
      break;
  }
  return "Unknown";
}

constexpr const char* toString(TextureFilter iFilter) {
  switch (iFilter) {
    case TextureFilter::Linear:
      return "Linear";
    case TextureFilter::Nearest:
      return "Nearest";
    case TextureFilter::Count:
      break;
  }
  return "Unknown";
}

// How a texture is sampled; SamplerManager has one sampler per combination
struct TextureSampler {
  TextureFilter filter = TextureFilter::Linear;
  TextureWrap wrapU = TextureWrap::Repeat; // glTF wrapS
  TextureWrap wrapV = TextureWrap::Repeat; // glTF wrapT
};

// A texture from ResourceManager::createTexture() and its sampler
struct TextureSlot {
  uint32_t texture = 0; // ResourceManager::kFallbackTextureIndex, the white texture, for an empty slot
  TextureSampler sampler;
};

// Nits for a unitless emissive of 1, in every format that has no unit; 1000 is Godot's default. One value for all of
// them, so the Emissive debug view, which divides it back out, shows each format's emissive as authored.
// shaders/scene.slang mirrors it.
constexpr float kUnitlessEmissiveNits = 1000.0f;

// A material's values other than its name and textures, shared by MaterialData and ModelMaterial. glTF
// metal-roughness; each factor multiplies its texture. The defaults are the engine's own: a dielectric.
struct MaterialParams {
  LinearColor baseColor = LinearColor::White;
  float metallic = 0.0f;
  float roughness = 0.5f;
  Vec3 emissive{0.0f}; // Nits
  float normalScale = 1.0f;
  float occlusionStrength = 1.0f;
  float alphaCutoff = 0.5f;
  AlphaMode alphaMode = AlphaMode::Opaque;
  bool doubleSided = false;
};

struct MaterialData {
  std::string name;
  MaterialParams params;
  TextureSlot baseColorTexture{};         // sRGB
  TextureSlot metallicRoughnessTexture{}; // Linear; G = roughness, B = metallic
  TextureSlot normalTexture{};            // Linear
  TextureSlot occlusionTexture{};         // Linear; R
  TextureSlot emissiveTexture{};          // sRGB
};

} // namespace ne
