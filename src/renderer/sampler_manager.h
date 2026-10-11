#pragma once

#include "renderer/rhi_types.h"
#include <array>
#include <cstdint>
#include <volk/volk.h>

namespace ne {

class Device;

class SamplerManager {
public:
  SamplerManager(Device* iDevice);
  ~SamplerManager();

  SamplerManager(const SamplerManager&) = delete;
  SamplerManager& operator=(const SamplerManager&) = delete;
  SamplerManager(SamplerManager&&) = delete;
  SamplerManager& operator=(SamplerManager&&) = delete;

  VkSampler get(SamplerType iType) const;

  // Indexed by SamplerType, for the bindless descriptor set
  const std::array<VkSampler, static_cast<size_t>(SamplerType::Count)>& getSamplers() const { return mSamplers; }

private:
  VkDevice mDevice = VK_NULL_HANDLE;

  std::array<VkSampler, static_cast<size_t>(SamplerType::Count)> mSamplers{};
};

} // namespace ne
