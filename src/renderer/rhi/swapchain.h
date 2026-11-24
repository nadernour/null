#pragma once

#include <volk/volk.h>

// std
#include <cstdint>
#include <vector>

namespace ne {

class Device;

class Swapchain {
public:
  struct Config {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    VkPresentModeKHR preferredPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    VkFormat preferredFormat = VK_FORMAT_B8G8R8A8_UNORM; // The tonemap encodes sRGB; ImGui's colors are sRGB already
    VkColorSpaceKHR preferredColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  };

  struct ImageResource {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
  };

  Swapchain(Device* iDevice, const Config& iConfig);
  ~Swapchain();

  Swapchain(const Swapchain&) = delete;
  Swapchain& operator=(const Swapchain&) = delete;
  Swapchain(Swapchain&&) = delete;
  Swapchain& operator=(Swapchain&&) = delete;

  VkResult acquireNextImage(VkSemaphore iSemaphore, uint32_t* oImageIndex, uint64_t iTimeout = UINT64_MAX);
  VkResult present(VkQueue iQueue, uint32_t iImageIndex, VkSemaphore iWaitSemaphore);

  void recreate(uint32_t iWidth, uint32_t iHeight);

  const Config& getConfig() const { return mConfig; }
  VkSurfaceKHR getSurface() const { return mConfig.surface; }
  VkSwapchainKHR getSwapchain() const { return mSwapchain; }
  VkExtent2D getExtent() const { return mExtent; }
  const VkSurfaceFormatKHR& getSurfaceFormat() const { return mSurfaceFormat; }
  VkPresentModeKHR getPresentMode() const { return mPresentMode; }

  const std::vector<ImageResource>& getImages() const { return mImages; }

private:
  void createSwapchain(VkSwapchainKHR iOldSwapchain = VK_NULL_HANDLE);
  void cleanup();

  Device* mDevice = nullptr;
  Config mConfig;

  VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;
  VkSurfaceFormatKHR mSurfaceFormat = {};
  VkExtent2D mExtent = {};
  VkPresentModeKHR mPresentMode = VK_PRESENT_MODE_FIFO_KHR;

  std::vector<ImageResource> mImages;
};

} // namespace ne
