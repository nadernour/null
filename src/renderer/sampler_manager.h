#pragma once

#include "core/material_data.h"
#include <array>
#include <cstdint>
#include <volk/volk.h>

namespace ne {

class Device;

// The immutable samplers of the bindless set: one per texture filter x U wrap x V wrap, then the shadow sampler
class SamplerManager {
public:
  static constexpr uint32_t kTextureFilterCount = static_cast<uint32_t>(TextureFilter::Count);
  static constexpr uint32_t kTextureWrapCount = static_cast<uint32_t>(TextureWrap::Count);
  static constexpr uint32_t kTextureSamplerCount = kTextureFilterCount * kTextureWrapCount * kTextureWrapCount;
  static constexpr uint32_t kShadowSamplerIndex = kTextureSamplerCount; // Linear, clamped to the border, reverse-Z compare
  static constexpr uint32_t kSamplerCount = kTextureSamplerCount + 1;

  // Index of the texture sampler with these settings, in getSamplers() and the shaders' sampler array
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

  // For the bindless descriptor set, indexed as above
  const std::array<VkSampler, kSamplerCount>& getSamplers() const { return mSamplers; }

private:
  VkDevice mDevice = VK_NULL_HANDLE;

  std::array<VkSampler, kSamplerCount> mSamplers{};
};

} // namespace ne
