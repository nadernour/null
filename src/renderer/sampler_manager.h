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

  // Index of the texture sampler with these settings, in getTextureSamplers() and the shaders' sampler array
  static constexpr uint32_t getTextureSamplerIndex(TextureFilter iFilter, TextureWrap iWrapU, TextureWrap iWrapV) {
    return (static_cast<uint32_t>(iFilter) * kTextureWrapCount + static_cast<uint32_t>(iWrapU)) * kTextureWrapCount +
           static_cast<uint32_t>(iWrapV);
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
