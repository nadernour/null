#pragma once

#include "core/material_data.h"
#include <cstdint>
#include <memory>

namespace ne {

class Pipeline;

// A material in ResourceManager's material buffer, with the pipeline that draws it. Immutable: its GPU data is written
// once, when ResourceManager creates it.
class Material {
public:
  Material(std::shared_ptr<Pipeline> iPipeline, uint32_t iMaterialIndex, AlphaMode iAlphaMode, bool iDoubleSided)
      : mPipeline(std::move(iPipeline)), mMaterialIndex(iMaterialIndex), mAlphaMode(iAlphaMode), mDoubleSided(iDoubleSided) {}
  ~Material() = default;

  Pipeline* getPipeline() const { return mPipeline.get(); }
  uint32_t getMaterialIndex() const { return mMaterialIndex; } // Into the material buffer
  AlphaMode getAlphaMode() const { return mAlphaMode; }
  bool isDoubleSided() const { return mDoubleSided; }

private:
  std::shared_ptr<Pipeline> mPipeline;
  uint32_t mMaterialIndex = 0;
  AlphaMode mAlphaMode = AlphaMode::Opaque;
  bool mDoubleSided = false;
};

} // namespace ne
