#include "renderer/device.h"
#include "core/assert.h"
#include "core/logger.h"
#include "renderer/instance.h"
#include "renderer/memory_allocator.h"
#include "renderer/utils.h"

// std
#include <algorithm>
#include <cstring>

namespace ne {

static Device::SwapChainSupportDetails querySwapChainSupportHelper(VkPhysicalDevice iDevice, VkSurfaceKHR iSurface) {
  Device::SwapChainSupportDetails oSwapChainSupportDetails;

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(iDevice, iSurface, &oSwapChainSupportDetails.capabilities);

  uint32_t formatCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR(iDevice, iSurface, &formatCount, nullptr);
  oSwapChainSupportDetails.formats.resize(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(iDevice, iSurface, &formatCount, oSwapChainSupportDetails.formats.data());

  uint32_t presentModeCount;
  vkGetPhysicalDeviceSurfacePresentModesKHR(iDevice, iSurface, &presentModeCount, nullptr);
  oSwapChainSupportDetails.presentModes.resize(presentModeCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(iDevice, iSurface, &presentModeCount, oSwapChainSupportDetails.presentModes.data());

  return oSwapChainSupportDetails;
}

Device::Device(Instance* iInstance, const Config& iConfig) {
  NE_ASSERT(iInstance != nullptr, "Instance must not be null");

  pickPhysicalDevice(iInstance->getInstance(), iConfig);
  createLogicalDevice(iConfig);
  mMemoryAllocator = std::make_unique<MemoryAllocator>(iInstance, this);
  createOneTimeCommandResources();
}

Device::~Device() {
  NE_VERBOSE("Destroying Vulkan Device and deallocating resources...");

  if (mOneTimeCommandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(mDevice, mOneTimeCommandPool, nullptr);
  }

  mMemoryAllocator.reset();

  vkDestroyDevice(mDevice, nullptr);
  NE_VERBOSE("Vulkan Device destroyed successfully.");
}

void Device::pickPhysicalDevice(VkInstance iInstance, const Config& iConfig) {
  NE_ASSERT(iInstance != VK_NULL_HANDLE, "Instance must not be null");
  uint32_t physicalDevicesCount;
  vkEnumeratePhysicalDevices(iInstance, &physicalDevicesCount, nullptr);
  NE_ASSERT(physicalDevicesCount != 0, "failed to find GPUs with Vulkan support!");

  std::vector<VkPhysicalDevice> physicalDevices(physicalDevicesCount);
  vkEnumeratePhysicalDevices(iInstance, &physicalDevicesCount, physicalDevices.data());

  uint32_t pickedDeviceScore = 0;
  for (VkPhysicalDevice& physicalDevice : physicalDevices) {
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
    const bool supportsVulkanApi = physicalDeviceProperties.apiVersion >= VK_API_VERSION_1_3;

    uint32_t physicalDeviceQueueIndex = ~0U;
    uint32_t deviceQueueFamilyPropertyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &deviceQueueFamilyPropertyCount, nullptr);
    std::vector<VkQueueFamilyProperties> deviceQueueFamilyProperties(deviceQueueFamilyPropertyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &deviceQueueFamilyPropertyCount, deviceQueueFamilyProperties.data());
    for (size_t queueIndex = 0; queueIndex < deviceQueueFamilyProperties.size(); queueIndex++) {
      const auto& queueFamily = deviceQueueFamilyProperties[queueIndex];
      if (queueFamily.queueCount <= 0) {
        break;
      }
      if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        if (iConfig.surface != VK_NULL_HANDLE) {
          VkBool32 presentSupport = false;
          vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, static_cast<uint32_t>(queueIndex), iConfig.surface,
                                               &presentSupport);
          if (presentSupport) {
            physicalDeviceQueueIndex = static_cast<uint32_t>(queueIndex);
            break;
          }
        } else {
          physicalDeviceQueueIndex = static_cast<uint32_t>(queueIndex);
          break;
        }
      }
    }
    const bool supportRequiredQueueFamilies = physicalDeviceQueueIndex != ~0U;

    uint32_t deviceExtensionPropertyCount;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionPropertyCount, nullptr);
    std::vector<VkExtensionProperties> deviceExtensionProperties(deviceExtensionPropertyCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionPropertyCount,
                                         deviceExtensionProperties.data());
    bool supportsAllRequiredExtensions = true;
    for (const auto& requiredDeviceExtension : iConfig.requiredExtensions) {
      bool extensionSupported = false;
      for (const auto& deviceExtension : deviceExtensionProperties) {
        if (strcmp(deviceExtension.extensionName, requiredDeviceExtension) == 0) {
          extensionSupported = true;
          break;
        }
      }
      if (!extensionSupported) {
        supportsAllRequiredExtensions = false;
        break;
      }
    }

    bool supportsSwapChain = false;
    if (iConfig.surface == VK_NULL_HANDLE) {
      supportsSwapChain = true;
    } else if (supportsAllRequiredExtensions) {
      SwapChainSupportDetails swapChainSupport = querySwapChainSupportHelper(physicalDevice, iConfig.surface);
      supportsSwapChain = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.pNext = nullptr;
    VkPhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.pNext = &vulkan13Features;
    VkPhysicalDeviceVulkan11Features vulkan11Features{};
    vulkan11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    vulkan11Features.pNext = &vulkan12Features;
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &vulkan11Features; // Start of the chain
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

    const bool supportsModernPipeline = vulkan13Features.dynamicRendering && vulkan13Features.synchronization2;

    // Vertex pulling and shader-shared structs through 64-bit buffer addresses
    const bool supportsBufferDeviceAddress = vulkan12Features.bufferDeviceAddress && vulkan12Features.scalarBlockLayout;

    // Multi-draw indirect with SV_DrawIndex
    const bool supportsGpuDrivenRendering = features2.features.multiDrawIndirect && vulkan11Features.shaderDrawParameters;

    const bool supportsTextureFiltering = features2.features.samplerAnisotropy;

    const bool supportsDescriptorIndexing =
        vulkan12Features.descriptorIndexing && vulkan12Features.shaderSampledImageArrayNonUniformIndexing &&
        vulkan12Features.descriptorBindingSampledImageUpdateAfterBind && vulkan12Features.descriptorBindingPartiallyBound &&
        vulkan12Features.runtimeDescriptorArray;

    const bool supportsRequiredFeatures = supportsModernPipeline && supportsBufferDeviceAddress && supportsGpuDrivenRendering &&
                                          supportsTextureFiltering && supportsDescriptorIndexing;

    if (!(supportsVulkanApi && supportRequiredQueueFamilies && supportsAllRequiredExtensions && supportsSwapChain &&
          supportsRequiredFeatures)) {
      continue;
    }

    uint32_t deviceScore = 1;
    if (physicalDeviceProperties.apiVersion >= VK_API_VERSION_1_4) {
      deviceScore += 10; // Slight preference for newer API
    }
    if (physicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      deviceScore += 100;
    }
    if (deviceScore > pickedDeviceScore) {
      pickedDeviceScore = deviceScore;
      mPhysicalDevice = physicalDevice;
      mPhysicalDeviceProperties = physicalDeviceProperties;
      mPhysicalDeviceQueueIndex = physicalDeviceQueueIndex;
    }
  }

  NE_ASSERT(mPhysicalDevice != nullptr, "Couldn't find suitable physical device.");
  vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &mPhysicalDeviceMemoryProperties);

  VkPhysicalDeviceDriverProperties driverProperties{};
  driverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
  VkPhysicalDeviceProperties2 properties2{};
  properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  properties2.pNext = &driverProperties;
  vkGetPhysicalDeviceProperties2(mPhysicalDevice, &properties2);
  const uint32_t apiVersion = mPhysicalDeviceProperties.apiVersion;
  NE_LOG("Selected GPU: {} (driver: {} {}, Vulkan {}.{}.{})", mPhysicalDeviceProperties.deviceName, driverProperties.driverName,
         driverProperties.driverInfo, VK_API_VERSION_MAJOR(apiVersion), VK_API_VERSION_MINOR(apiVersion),
         VK_API_VERSION_PATCH(apiVersion));
}

void Device::createLogicalDevice(const Config& iConfig) {
  NE_ASSERT(mPhysicalDeviceQueueIndex != ~0U, "Couldn't find the queue for graphics and present.");
  float queuePriority = 0.5f;
  VkDeviceQueueCreateInfo deviceQueueCreateInfo{};
  deviceQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  deviceQueueCreateInfo.queueFamilyIndex = mPhysicalDeviceQueueIndex;
  deviceQueueCreateInfo.queueCount = 1;
  deviceQueueCreateInfo.pQueuePriorities = &queuePriority;
  VkPhysicalDeviceVulkan13Features vulkan13Features{};
  vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  vulkan13Features.pNext = nullptr;
  vulkan13Features.dynamicRendering = VK_TRUE;
  vulkan13Features.synchronization2 = VK_TRUE;
  VkPhysicalDeviceVulkan12Features vulkan12Features{};
  vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  vulkan12Features.pNext = &vulkan13Features;
  vulkan12Features.bufferDeviceAddress = VK_TRUE;
  vulkan12Features.scalarBlockLayout = VK_TRUE;
  vulkan12Features.descriptorIndexing = VK_TRUE;
  vulkan12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  vulkan12Features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
  vulkan12Features.descriptorBindingPartiallyBound = VK_TRUE;
  vulkan12Features.runtimeDescriptorArray = VK_TRUE;
  VkPhysicalDeviceVulkan11Features vulkan11Features{};
  vulkan11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
  vulkan11Features.pNext = &vulkan12Features;
  vulkan11Features.shaderDrawParameters = VK_TRUE;
  VkPhysicalDeviceFeatures2 deviceFeatures{};
  deviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  deviceFeatures.pNext = &vulkan11Features; // Start of the chain
  deviceFeatures.features.multiDrawIndirect = VK_TRUE;
  deviceFeatures.features.samplerAnisotropy = VK_TRUE;
  VkDeviceCreateInfo deviceCreateInfo{};
  deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCreateInfo.pNext = &deviceFeatures;
  deviceCreateInfo.queueCreateInfoCount = 1;
  deviceCreateInfo.pQueueCreateInfos = &deviceQueueCreateInfo;
  deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(iConfig.requiredExtensions.size());
  deviceCreateInfo.ppEnabledExtensionNames = iConfig.requiredExtensions.data();
  deviceCreateInfo.pEnabledFeatures = nullptr;

  VK_CHECK(vkCreateDevice(mPhysicalDevice, &deviceCreateInfo, nullptr, &mDevice));
  volkLoadDevice(mDevice);

  vkGetDeviceQueue(mDevice, mPhysicalDeviceQueueIndex, 0, &mQueue);
  vk_utils::setDebugObjectName(mDevice, mQueue, "Main_GraphicsQueue");
  NE_VERBOSE("Vulkan logical device created successfully.");
}

void Device::createOneTimeCommandResources() {
  VkCommandPoolCreateInfo oneTimePoolInfo{};
  oneTimePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  oneTimePoolInfo.queueFamilyIndex = mPhysicalDeviceQueueIndex;
  oneTimePoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  VK_CHECK(vkCreateCommandPool(mDevice, &oneTimePoolInfo, nullptr, &mOneTimeCommandPool));

  VkCommandBufferAllocateInfo oneTimeAllocInfo{};
  oneTimeAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  oneTimeAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  oneTimeAllocInfo.commandPool = mOneTimeCommandPool;
  oneTimeAllocInfo.commandBufferCount = 1;
  VK_CHECK(vkAllocateCommandBuffers(mDevice, &oneTimeAllocInfo, &mOneTimeCommandBuffer));

  vk_utils::setDebugObjectName(mDevice, mOneTimeCommandPool, "OneTime_CommandPool");
  vk_utils::setDebugObjectName(mDevice, mOneTimeCommandBuffer, "OneTime_CommandBuffer");
}

VkCommandBuffer Device::beginOneTimeCommand() {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  VK_CHECK(vkBeginCommandBuffer(mOneTimeCommandBuffer, &beginInfo));

  return mOneTimeCommandBuffer;
}

void Device::endOneTimeCommand(VkCommandBuffer iCommandBuffer) {
  NE_ASSERT(iCommandBuffer == mOneTimeCommandBuffer);
  VK_CHECK(vkEndCommandBuffer(iCommandBuffer));

  VkCommandBufferSubmitInfo commandBufferInfo{};
  commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  commandBufferInfo.commandBuffer = iCommandBuffer;

  VkSubmitInfo2 submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  submitInfo.commandBufferInfoCount = 1;
  submitInfo.pCommandBufferInfos = &commandBufferInfo;

  VK_CHECK(vkQueueSubmit2(mQueue, 1, &submitInfo, VK_NULL_HANDLE));
  VK_CHECK(vkQueueWaitIdle(mQueue));

  VK_CHECK(vkResetCommandPool(mDevice, mOneTimeCommandPool, 0));
}

Device::SwapChainSupportDetails Device::querySwapChainSupport(VkSurfaceKHR iSurface) const {
  NE_ASSERT(iSurface != VK_NULL_HANDLE, "Surface must not be null when querying swapchain support");
  return querySwapChainSupportHelper(mPhysicalDevice, iSurface);
}

VkFormat Device::findDepthFormat() const {
  VkFormat depthFormat =
      findSupportedFormat({VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT},
                          VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  if (depthFormat != VK_FORMAT_D32_SFLOAT_S8_UINT) {
    NE_WARN("Preferred depth format D32_SFLOAT_S8_UINT unsupported; falling back to {}, which loses Reverse-Z precision",
            string_VkFormat(depthFormat));
  }
  return depthFormat;
}

VkFormat Device::findSupportedFormat(const std::vector<VkFormat>& iCandidates, VkImageTiling iTiling,
                                     VkFormatFeatureFlags iFeatures) const {
  for (const auto format : iCandidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, format, &props);
    if ((iTiling == VK_IMAGE_TILING_OPTIMAL) && ((props.optimalTilingFeatures & iFeatures) == iFeatures)) {
      return format;
    }
  }
  NE_ASSERT(false, "Failed to find supported format!");
  return VK_FORMAT_UNDEFINED;
}

void Device::waitIdle() const { vkDeviceWaitIdle(mDevice); }

uint32_t Device::findMemoryType(uint32_t iTypeFilter, VkMemoryPropertyFlags iProperties) const {
  for (uint32_t i = 0; i < mPhysicalDeviceMemoryProperties.memoryTypeCount; ++i) {
    if ((iTypeFilter & (1 << i)) && (mPhysicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & iProperties) == iProperties) {
      return i;
    }
  }
  return ~0U;
}

} // namespace ne
