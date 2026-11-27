#pragma once

#include "core/material_data.h"
#include <array>
#include <cstdint>
#include <volk/volk.h>

namespace ne {

class Device;

// The immutable samplers of the bindless set: one per texture filter x U wrap x V wrap, and the shadow sampler, which
// compares depths
class SamplerManager {
public:
  static constexpr uint32_t kTextureFilterCount = static_cast<uint32_t>(TextureFilter::Count);
  static constexpr uint32_t kTextureWrapCount = static_cast<uint32_t>(TextureWrap::Count);
  static constexpr uint32_t kTextureSamplerCount = kTextureFilterCount * kTextureWrapCount * kTextureWrapCount;
  static_assert(kTextureSamplerCount <= 0x10000, "gpu::packTexture keeps sampler indices in 16 bits");

  // Index of iSampler's sampler, in getTextureSamplers() and the shaders' sampler array
  static constexpr uint32_t getTextureSamplerIndex(const TextureSampler& iSampler) {
    const auto filter = static_cast<uint32_t>(iSampler.filter);
    const auto wrapU = static_cast<uint32_t>(iSampler.wrapU);
    const auto wrapV = static_cast<uint32_t>(iSampler.wrapV);
    return (filter * kTextureWrapCount + wrapU) * kTextureWrapCount + wrapV;
  }

  SamplerManager(Device* iDevice);
  ~SamplerManager();

  SamplerManager(const SamplerManager&) = delete;
  SamplerManager& operator=(const SamplerManager&) = delete;
  SamplerManager(SamplerManager&&) = delete;
  SamplerManager& operator=(SamplerManager&&) = delete;

  // The bindless set's sampler array (binding 0), indexed as above
  const std::array<VkSampler, kTextureSamplerCount>& getTextureSamplers() const { return mTextureSamplers; }
  // The bindless set's binding 2: linear, clamped to the border, with a reverse-Z depth comparison
  VkSampler getShadowSampler() const { return mShadowSampler; }

private:
  VkDevice mDevice = VK_NULL_HANDLE;

  std::array<VkSampler, kTextureSamplerCount> mTextureSamplers{};
  VkSampler mShadowSampler = VK_NULL_HANDLE;
};

} // namespace ne
