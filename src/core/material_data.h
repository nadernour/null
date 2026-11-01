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

// A material's texture and its sampler settings; image indexes ModelData::images, -1 for none
struct TextureRef {
  int32_t image = -1;
  TextureWrap wrapU = TextureWrap::Repeat; // glTF wrapS
  TextureWrap wrapV = TextureWrap::Repeat; // glTF wrapT
  TextureFilter filter = TextureFilter::Linear;
};

// Emissive has no unit in glTF (or in OBJ, DAE, and MJCF, which come later), so importers give an emissive of 1 this
// many nits. With one value for every format, the Emissive debug view can divide it back out and show any file's
// emissive as authored, as the Khronos Sample Viewer does for glTF. shaders/scene.slang mirrors it.
constexpr float kUnitlessEmissiveNits = 1000.0f;

// glTF metal-roughness; each factor multiplies its texture. The defaults are the engine's own: a dielectric.
struct MaterialData {
  std::string name;
  LinearColor baseColor = LinearColor::White;
  float metallic = 0.0f;
  float roughness = 0.5f;
  Vec3 emissive{0.0f}; // Nits
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
