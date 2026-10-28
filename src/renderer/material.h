#pragma once

#include "core/material_data.h"
#include <cstdint>

namespace ne {

// A material in ResourceManager's material buffer. Immutable: its GPU data is written once, when ResourceManager creates
// it. SceneRenderer draws it with the pipeline for its alpha mode, and without culling when it's double-sided.
class Material {
public:
  Material(uint32_t iMaterialIndex, AlphaMode iAlphaMode, bool iDoubleSided)
      : mMaterialIndex(iMaterialIndex), mAlphaMode(iAlphaMode), mDoubleSided(iDoubleSided) {}
  ~Material() = default;

  uint32_t getMaterialIndex() const { return mMaterialIndex; } // Into the material buffer
  AlphaMode getAlphaMode() const { return mAlphaMode; }
  bool isDoubleSided() const { return mDoubleSided; }

private:
  uint32_t mMaterialIndex = 0;
  AlphaMode mAlphaMode = AlphaMode::Opaque;
  bool mDoubleSided = false;
};

} // namespace ne
