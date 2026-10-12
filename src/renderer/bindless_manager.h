#pragma once

#include "renderer/utils.h"
#include <cstdint>
#include <volk/volk.h>

namespace ne {

class SamplerManager;

class BindlessManager {
public:
  BindlessManager(VkDevice iDevice, SamplerManager* iSamplerManager, uint32_t iMaxSampledImages = vk_utils::kMaxSampledImages);
  ~BindlessManager();

  BindlessManager(const BindlessManager&) = delete;
  BindlessManager& operator=(const BindlessManager&) = delete;
  BindlessManager(BindlessManager&&) = delete;
  BindlessManager& operator=(BindlessManager&&) = delete;

  uint32_t registerSampledImage(VkImageView iImageView);
  void bind(VkCommandBuffer iCommandBuffer, VkPipelineLayout iPipelineLayout);

  VkDescriptorSetLayout getDescriptorSetLayout() const { return mDescriptorSetLayout; }
  VkDescriptorSet getDescriptorSet() const { return mDescriptorSet; }
  uint32_t getRegisteredSampledImageCount() const { return mNextSampledImageIndex; }
  uint32_t getMaxSampledImages() const { return mMaxSampledImages; }
  bool isFull() const { return mNextSampledImageIndex >= mMaxSampledImages; }

private:
  VkDevice mDevice = VK_NULL_HANDLE;
  uint32_t mMaxSampledImages = vk_utils::kMaxSampledImages;
  uint32_t mNextSampledImageIndex = 0;
  VkDescriptorPool mDescriptorPool = VK_NULL_HANDLE;
  VkDescriptorSetLayout mDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorSet mDescriptorSet = VK_NULL_HANDLE;
};

} // namespace ne
