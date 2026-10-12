#pragma once

#include "core/assert.h"
#include "core/defines.h"
#include "core/logger.h"

#include <volk/volk.h>
#include <vulkan/vk_enum_string_helper.h>

#include <cstdlib>
#include <string>
#include <type_traits>

// Logs the failed call before aborting in every build, so a headless run that dies leaves a reason behind
#define VK_CHECK(fn)                                                                                                             \
  do {                                                                                                                           \
    VkResult vkCheckResult = (fn);                                                                                               \
    if (vkCheckResult != VK_SUCCESS) {                                                                                           \
      ne::Logger::log(ne::Logger::LogLevel::Fatal,                                                                               \
                      "VULKAN ERROR\n"                                                                                           \
                      "  Function: {}\n"                                                                                         \
                      "  Result: {} ({})\n"                                                                                      \
                      "  File: {}\n"                                                                                             \
                      "  Line: {}",                                                                                              \
                      #fn, string_VkResult(vkCheckResult), static_cast<int>(vkCheckResult), __FILE__, __LINE__);                 \
      NE_BREAK();                                                                                                                \
      std::abort();                                                                                                              \
    }                                                                                                                            \
  } while (false)

namespace ne::vk_utils {

constexpr VkDeviceSize kVertexPoolSize = 64 * 1024 * 1024;
constexpr VkDeviceSize kIndexPoolSize = 32 * 1024 * 1024;
constexpr VkDeviceSize kUploadBufferSize = 16 * 1024 * 1024;
constexpr VkDeviceSize kStagingBufferSize = 64 * 1024 * 1024;
constexpr uint32_t kMaxSampledImages = 4096;
constexpr const char* kDefaultShader = "base_shader";

inline void setDebugUtilsObjectName([[maybe_unused]] VkDevice iDevice, [[maybe_unused]] VkObjectType iObjectType,
                                    [[maybe_unused]] uint64_t iObjectHandle, [[maybe_unused]] const char* iName) {
#ifndef NE_BUILD_SHIPPING
  if (vkSetDebugUtilsObjectNameEXT && iDevice != VK_NULL_HANDLE && iObjectHandle != 0 && iName != nullptr) {
    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = iObjectType;
    nameInfo.objectHandle = iObjectHandle;
    nameInfo.pObjectName = iName;
    vkSetDebugUtilsObjectNameEXT(iDevice, &nameInfo);
  }
#endif
}

inline void setDebugUtilsObjectName(VkDevice iDevice, VkObjectType iObjectType, uint64_t iObjectHandle,
                                    const std::string& iName) {
  setDebugUtilsObjectName(iDevice, iObjectType, iObjectHandle, iName.c_str());
}

template <typename T>
inline void setDebugObjectName(VkDevice iDevice, T iHandle, const char* iName) {
  VkObjectType type = VK_OBJECT_TYPE_UNKNOWN;
  if constexpr (std::is_same_v<T, VkBuffer>)
    type = VK_OBJECT_TYPE_BUFFER;
  else if constexpr (std::is_same_v<T, VkDeviceMemory>)
    type = VK_OBJECT_TYPE_DEVICE_MEMORY;
  else if constexpr (std::is_same_v<T, VkImage>)
    type = VK_OBJECT_TYPE_IMAGE;
  else if constexpr (std::is_same_v<T, VkImageView>)
    type = VK_OBJECT_TYPE_IMAGE_VIEW;
  else if constexpr (std::is_same_v<T, VkPipeline>)
    type = VK_OBJECT_TYPE_PIPELINE;
  else if constexpr (std::is_same_v<T, VkPipelineLayout>)
    type = VK_OBJECT_TYPE_PIPELINE_LAYOUT;
  else if constexpr (std::is_same_v<T, VkSampler>)
    type = VK_OBJECT_TYPE_SAMPLER;
  else if constexpr (std::is_same_v<T, VkCommandPool>)
    type = VK_OBJECT_TYPE_COMMAND_POOL;
  else if constexpr (std::is_same_v<T, VkCommandBuffer>)
    type = VK_OBJECT_TYPE_COMMAND_BUFFER;
  else if constexpr (std::is_same_v<T, VkQueue>)
    type = VK_OBJECT_TYPE_QUEUE;
  else if constexpr (std::is_same_v<T, VkSemaphore>)
    type = VK_OBJECT_TYPE_SEMAPHORE;
  else if constexpr (std::is_same_v<T, VkFence>)
    type = VK_OBJECT_TYPE_FENCE;
  else if constexpr (std::is_same_v<T, VkShaderModule>)
    type = VK_OBJECT_TYPE_SHADER_MODULE;
  else if constexpr (std::is_same_v<T, VkSwapchainKHR>)
    type = VK_OBJECT_TYPE_SWAPCHAIN_KHR;
  else if constexpr (std::is_same_v<T, VkDescriptorSetLayout>)
    type = VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
  else if constexpr (std::is_same_v<T, VkDescriptorPool>)
    type = VK_OBJECT_TYPE_DESCRIPTOR_POOL;
  else if constexpr (std::is_same_v<T, VkDescriptorSet>)
    type = VK_OBJECT_TYPE_DESCRIPTOR_SET;
  else {
    static_assert(!sizeof(T), "Unsupported Vulkan object type passed to setDebugObjectName!");
  }

  setDebugUtilsObjectName(iDevice, type, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(iHandle)), iName);
}

template <typename T>
inline void setDebugObjectName(VkDevice iDevice, T iHandle, const std::string& iName) {
  setDebugObjectName(iDevice, iHandle, iName.c_str());
}

template <typename T>
constexpr T alignUp(T iValue, T iAlignment) {
  return (iValue + iAlignment - 1) & ~(iAlignment - 1);
}

inline std::string formatBytes(VkDeviceSize iBytes) {
  constexpr double kKilobyte = 1024.0;
  constexpr double kMegabyte = 1024.0 * 1024.0;
  constexpr double kGigabyte = 1024.0 * 1024.0 * 1024.0;

  if (iBytes >= kGigabyte) {
    return std::format("{:.2f} GB", iBytes / kGigabyte);
  } else if (iBytes >= kMegabyte) {
    return std::format("{:.2f} MB", iBytes / kMegabyte);
  } else if (iBytes >= kKilobyte) {
    return std::format("{:.2f} KB", iBytes / kKilobyte);
  } else {
    return std::format("{} B", iBytes);
  }
}

inline VkImageAspectFlags deduceAspectFlags(VkFormat iFormat) {
  switch (iFormat) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
      return VK_IMAGE_ASPECT_DEPTH_BIT;
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    case VK_FORMAT_S8_UINT:
      return VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
      return VK_IMAGE_ASPECT_COLOR_BIT;
  }
}

inline void transitionImageLayout(VkCommandBuffer iCommandBuffer, VkImage iImage,
                                  const VkImageSubresourceRange& iSubresourceRange, VkImageLayout iOldLayout,
                                  VkImageLayout iNewLayout, VkAccessFlags2 iSrcAccessMask, VkAccessFlags2 iDstAccessMask,
                                  VkPipelineStageFlags2 iSrcStageMask, VkPipelineStageFlags2 iDstStageMask) {
  VkImageMemoryBarrier2 imageMemoryBarrier{};
  imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  imageMemoryBarrier.srcStageMask = iSrcStageMask;
  imageMemoryBarrier.srcAccessMask = iSrcAccessMask;
  imageMemoryBarrier.dstStageMask = iDstStageMask;
  imageMemoryBarrier.dstAccessMask = iDstAccessMask;
  imageMemoryBarrier.oldLayout = iOldLayout;
  imageMemoryBarrier.newLayout = iNewLayout;
  imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  imageMemoryBarrier.image = iImage;
  imageMemoryBarrier.subresourceRange = iSubresourceRange;

  VkDependencyInfo dependencyInfo{};
  dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependencyInfo.dependencyFlags = 0;
  dependencyInfo.imageMemoryBarrierCount = 1;
  dependencyInfo.pImageMemoryBarriers = &imageMemoryBarrier;

  vkCmdPipelineBarrier2(iCommandBuffer, &dependencyInfo);
}

} // namespace ne::vk_utils
