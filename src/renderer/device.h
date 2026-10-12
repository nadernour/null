#pragma once

#include <volk/volk.h>

#include <memory>
#include <string>
#include <vector>

namespace ne {

class Instance;
class MemoryAllocator;

class Device {
public:
  struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
  };

  struct Config {
    VkSurfaceKHR surface = VK_NULL_HANDLE; // Optional: if null, runs headlessly
    std::vector<const char*> requiredExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  };

  Device(Instance* iInstance, const Config& iConfig);
  ~Device();

  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  Device(Device&&) = delete;
  Device& operator=(Device&&) = delete;

  VkDevice getDevice() const { return mDevice; }
  VkPhysicalDevice getPhysicalDevice() const { return mPhysicalDevice; }
  VkQueue getQueue() const { return mQueue; }
  uint32_t getQueueFamilyIndex() const { return mPhysicalDeviceQueueIndex; }
  const VkPhysicalDeviceProperties& getPhysicalDeviceProperties() const { return mPhysicalDeviceProperties; }
  const VkPhysicalDeviceMemoryProperties& getPhysicalDeviceMemoryProperties() const { return mPhysicalDeviceMemoryProperties; }

  uint32_t findMemoryType(uint32_t iTypeFilter, VkMemoryPropertyFlags iProperties) const;

  void waitIdle() const;

  VkCommandBuffer beginOneTimeCommand();
  void endOneTimeCommand(VkCommandBuffer iCommandBuffer);

  SwapChainSupportDetails querySwapChainSupport(VkSurfaceKHR iSurface) const;
  VkFormat findSupportedFormat(const std::vector<VkFormat>& iCandidates, VkImageTiling iTiling,
                               VkFormatFeatureFlags iFeatures) const;
  VkFormat findDepthFormat() const;

  MemoryAllocator* getMemoryAllocator() const { return mMemoryAllocator.get(); }

private:
  void pickPhysicalDevice(VkInstance iInstance, const Config& iConfig);
  void createLogicalDevice(const Config& iConfig);
  void createOneTimeCommandResources();

  std::unique_ptr<MemoryAllocator> mMemoryAllocator;

  VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties mPhysicalDeviceProperties = {};
  VkPhysicalDeviceMemoryProperties mPhysicalDeviceMemoryProperties = {};
  uint32_t mPhysicalDeviceQueueIndex = ~0U;
  VkDevice mDevice = VK_NULL_HANDLE;
  VkQueue mQueue = VK_NULL_HANDLE;

  VkCommandPool mOneTimeCommandPool = VK_NULL_HANDLE;
  VkCommandBuffer mOneTimeCommandBuffer = VK_NULL_HANDLE;
};

} // namespace ne
