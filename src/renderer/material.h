#pragma once

#include "renderer/rhi_types.h"
#include <cstdint>
#include <memory>

namespace ne {

class Pipeline;

class Material {
public:
  Material(std::shared_ptr<Pipeline> iPipeline, uint32_t iTextureIndex) : mPipeline(iPipeline), mTextureIndex(iTextureIndex) {}
  ~Material() = default;

  Pipeline* getPipeline() const { return mPipeline.get(); }

  void setTexture(uint32_t iTextureId, SamplerType iSampler = SamplerType::LinearRepeat) {
    mTextureIndex = iTextureId;
    mSamplerType = iSampler;
  }

  void setSampler(SamplerType iSampler) { mSamplerType = iSampler; }

  uint32_t getTextureIndex() const { return mTextureIndex; }
  SamplerType getSamplerType() const { return mSamplerType; }

private:
  std::shared_ptr<Pipeline> mPipeline;
  uint32_t mTextureIndex;
  SamplerType mSamplerType = SamplerType::LinearRepeat;
};

} // namespace ne
