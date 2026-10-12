#pragma once

#include "core/assert.h"
#include "core/defines.h"
#include "core/logger.h"

#include <volk/volk.h>
#ifndef NE_BUILD_SHIPPING
#include <vulkan/vk_enum_string_helper.h>
#endif

#include <cstdlib>
#include <string>
#include <type_traits>

#ifndef NE_BUILD_SHIPPING

#define VK_CHECK(fn)                                                                                                             \
  do {                                                                                                                           \
    VkResult vkCheckResult = (fn);                                                                                               \
    if (vkCheckResult != VK_SUCCESS) {                                                                                           \
      ne::Logger::log(ne::Logger::LogType::Fatal,                                                                                \
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

#else

#define VK_CHECK(fn)                                                                                                             \
  do {                                                                                                                           \
    VkResult vkCheckResult = (fn);                                                                                               \
    if (vkCheckResult != VK_SUCCESS) {                                                                                           \
      std::abort();                                                                                                              \
    }                                                                                                                            \
  } while (false)

#endif

namespace ne::vk_utils {

constexpr VkDeviceSize kVertexPoolSize = 64 * 1024 * 1024;
constexpr VkDeviceSize kIndexPoolSize = 32 * 1024 * 1024;
constexpr VkDeviceSize kUploadBufferSize = 16 * 1024 * 1024;
constexpr VkDeviceSize kStagingBufferSize = 64 * 1024 * 1024;
constexpr uint32_t kMaxSampledImages = 4096;
constexpr const char* kDefaultShader = "base_shader";

inline void setDebugUtilsObjectName(VkDevice device, VkObjectType objectType, uint64_t objectHandle, const char* name) {
#ifndef NE_BUILD_SHIPPING
  if (vkSetDebugUtilsObjectNameEXT && device != VK_NULL_HANDLE && objectHandle != 0 && name != nullptr) {
    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = objectType;
    nameInfo.objectHandle = objectHandle;
    nameInfo.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(device, &nameInfo);
  }
#else
  NE_UNUSED(device);
  NE_UNUSED(objectType);
  NE_UNUSED(objectHandle);
  NE_UNUSED(name);
#endif
}

inline void setDebugUtilsObjectName(VkDevice device, VkObjectType objectType, uint64_t objectHandle, const std::string& name) {
  setDebugUtilsObjectName(device, objectType, objectHandle, name.c_str());
}

template <typename T>
inline void setDebugObjectName(VkDevice device, T handle, const char* name) {
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

  setDebugUtilsObjectName(device, type, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle)), name);
}

template <typename T>
inline void setDebugObjectName(VkDevice device, T handle, const std::string& name) {
  setDebugObjectName(device, handle, name.c_str());
}

template <typename T>
constexpr T alignUp(T value, T alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

inline std::string formatBytes(VkDeviceSize bytes) {
  constexpr double kKilobyte = 1024.0;
  constexpr double kMegabyte = 1024.0 * 1024.0;
  constexpr double kGigabyte = 1024.0 * 1024.0 * 1024.0;

  if (bytes >= kGigabyte) {
    return std::format("{:.2f} GB", bytes / kGigabyte);
  } else if (bytes >= kMegabyte) {
    return std::format("{:.2f} MB", bytes / kMegabyte);
  } else if (bytes >= kKilobyte) {
    return std::format("{:.2f} KB", bytes / kKilobyte);
  } else {
    return std::format("{} B", bytes);
  }
}

inline VkImageAspectFlags deduceAspectFlags(VkFormat format) {
  switch (format) {
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

inline void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, const VkImageSubresourceRange& subresourceRange,
                                  VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags2 srcAccessMask,
                                  VkAccessFlags2 dstAccessMask, VkPipelineStageFlags2 srcStageMask,
                                  VkPipelineStageFlags2 dstStageMask) {
  VkImageMemoryBarrier2 imageMemoryBarrier{};
  imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  imageMemoryBarrier.srcStageMask = srcStageMask;
  imageMemoryBarrier.srcAccessMask = srcAccessMask;
  imageMemoryBarrier.dstStageMask = dstStageMask;
  imageMemoryBarrier.dstAccessMask = dstAccessMask;
  imageMemoryBarrier.oldLayout = oldLayout;
  imageMemoryBarrier.newLayout = newLayout;
  imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  imageMemoryBarrier.image = image;
  imageMemoryBarrier.subresourceRange = subresourceRange;

  VkDependencyInfo dependencyInfo{};
  dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependencyInfo.dependencyFlags = 0;
  dependencyInfo.imageMemoryBarrierCount = 1;
  dependencyInfo.pImageMemoryBarriers = &imageMemoryBarrier;

  vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

} // namespace ne::vk_utils
