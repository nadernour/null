#include "renderer/bindless_manager.h"
#include "renderer/sampler_manager.h"
#include "renderer/utils.h"
#include <array>

namespace ne {

BindlessManager::BindlessManager(VkDevice iDevice, SamplerManager* iSamplerManager, uint32_t iMaxSampledImages)
    : mDevice(iDevice), mMaxSampledImages(iMaxSampledImages) {
  NE_ASSERT(mDevice != VK_NULL_HANDLE, "Device must not be VK_NULL_HANDLE");
  NE_ASSERT(iSamplerManager != nullptr, "SamplerManager cannot be null in BindlessManager");

  const uint32_t samplerCount = static_cast<uint32_t>(iSamplerManager->getSamplers().size());

  // Set 0: binding 0 holds one immutable sampler per SamplerType, binding 1 the sampled image array
  std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  bindings[0].descriptorCount = samplerCount;
  bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings[0].pImmutableSamplers = iSamplerManager->getSamplers().data();

  bindings[1].binding = 1;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  bindings[1].descriptorCount = mMaxSampledImages;
  bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings[1].pImmutableSamplers = nullptr;

  std::array<VkDescriptorBindingFlags, 2> bindingFlags{};
  bindingFlags[0] = 0;
  bindingFlags[1] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

  VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
  bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
  bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
  bindingFlagsInfo.pBindingFlags = bindingFlags.data();

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.pNext = &bindingFlagsInfo;
  layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  VK_CHECK(vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr, &mDescriptorSetLayout));
  vk_utils::setDebugObjectName(mDevice, mDescriptorSetLayout, "Bindless_DescriptorSetLayout");

  std::array<VkDescriptorPoolSize, 2> poolSizes{};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLER;
  poolSizes[0].descriptorCount = samplerCount;
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  poolSizes[1].descriptorCount = mMaxSampledImages;

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
  poolInfo.maxSets = 1;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();

  VK_CHECK(vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &mDescriptorPool));
  vk_utils::setDebugObjectName(mDevice, mDescriptorPool, "Bindless_DescriptorPool");

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = mDescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &mDescriptorSetLayout;

  VK_CHECK(vkAllocateDescriptorSets(mDevice, &allocInfo, &mDescriptorSet));
  vk_utils::setDebugObjectName(mDevice, mDescriptorSet, "Bindless_DescriptorSet");

  NE_VERBOSE("Initialized BindlessManager: {} samplers, {} max sampled images", samplerCount, mMaxSampledImages);
}

BindlessManager::~BindlessManager() {
  if (mDescriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(mDevice, mDescriptorPool, nullptr);
    mDescriptorPool = VK_NULL_HANDLE;
    mDescriptorSet = VK_NULL_HANDLE;
  }
  if (mDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(mDevice, mDescriptorSetLayout, nullptr);
    mDescriptorSetLayout = VK_NULL_HANDLE;
  }
  NE_VERBOSE("Destroyed BindlessManager resources");
}

uint32_t BindlessManager::registerSampledImage(VkImageView iImageView) {
  NE_ASSERT(iImageView != VK_NULL_HANDLE, "ImageView must be valid to register for bindless sampling");
  NE_ASSERT(!isFull(), "Exceeded maximum bindless sampled images");

  uint32_t index = mNextSampledImageIndex++;

  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageView = iImageView;
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = mDescriptorSet;
  write.dstBinding = 1;
  write.dstArrayElement = index;
  write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;

  vkUpdateDescriptorSets(mDevice, 1, &write, 0, nullptr);
  return index;
}

void BindlessManager::bind(VkCommandBuffer iCommandBuffer, VkPipelineLayout iPipelineLayout) {
  NE_ASSERT(iCommandBuffer != VK_NULL_HANDLE, "Command buffer must not be null");
  NE_ASSERT(iPipelineLayout != VK_NULL_HANDLE, "Pipeline layout must not be null");
  vkCmdBindDescriptorSets(iCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, iPipelineLayout, 0, 1, &mDescriptorSet, 0, nullptr);
}

} // namespace ne
