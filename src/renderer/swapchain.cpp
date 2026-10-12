#include "renderer/swapchain.h"
#include "core/assert.h"
#include "core/logger.h"
#include "core/math/math_utils.h"
#include "renderer/device.h"
#include "renderer/utils.h"

// std
#include <format>

namespace ne {

Swapchain::Swapchain(Device* iDevice, const Config& iConfig) : mDevice(iDevice), mConfig(iConfig) {
  NE_ASSERT(mDevice != nullptr, "Device must not be null");
  NE_ASSERT(mConfig.surface != VK_NULL_HANDLE, "Surface must not be null");
  NE_ASSERT(mConfig.width > 0 && mConfig.height > 0, "Swapchain dimensions must be positive");

  createSwapchain(VK_NULL_HANDLE);
}

Swapchain::~Swapchain() {
  NE_LOG("Destroying Vulkan Swapchain and deallocating resources...");
  cleanup();
  NE_LOG("Vulkan Swapchain destroyed successfully.");
}

void Swapchain::cleanup() {
  if (mDevice == nullptr || mDevice->getDevice() == VK_NULL_HANDLE) {
    return;
  }

  for (ImageResource& image : mImages) {
    if (image.view != VK_NULL_HANDLE) {
      vkDestroyImageView(mDevice->getDevice(), image.view, nullptr);
      image.view = VK_NULL_HANDLE;
    }
    if (image.renderFinishedSemaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(mDevice->getDevice(), image.renderFinishedSemaphore, nullptr);
      image.renderFinishedSemaphore = VK_NULL_HANDLE;
    }
  }
  mImages.clear();

  if (mSwapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(mDevice->getDevice(), mSwapchain, nullptr);
    mSwapchain = VK_NULL_HANDLE;
  }
}

void Swapchain::createSwapchain(VkSwapchainKHR iOldSwapchain) {
  Device::SwapChainSupportDetails swapChainSupport = mDevice->querySwapChainSupport(mConfig.surface);
  const VkSurfaceCapabilitiesKHR& surfaceCapabilities = swapChainSupport.capabilities;

  NE_ASSERT(!swapChainSupport.formats.empty(), "No surface formats found");
  VkSurfaceFormatKHR selectedSurfaceFormat = swapChainSupport.formats[0];
  for (const auto& surfaceFormat : swapChainSupport.formats) {
    if (surfaceFormat.format == mConfig.preferredFormat && surfaceFormat.colorSpace == mConfig.preferredColorSpace) {
      selectedSurfaceFormat = surfaceFormat;
      break;
    }
  }

  NE_ASSERT(!swapChainSupport.presentModes.empty(), "No present modes found");
  VkPresentModeKHR selectedPresentMode = swapChainSupport.presentModes[0];
  for (const auto& presentMode : swapChainSupport.presentModes) {
    if (presentMode == mConfig.preferredPresentMode) {
      selectedPresentMode = presentMode;
      break;
    }
    if (presentMode == VK_PRESENT_MODE_FIFO_KHR) {
      selectedPresentMode = presentMode;
    }
  }
  NE_ASSERT(selectedPresentMode == VK_PRESENT_MODE_MAILBOX_KHR || selectedPresentMode == VK_PRESENT_MODE_FIFO_KHR,
            "Unsupported present mode");

  VkExtent2D selectedSwapExtent = surfaceCapabilities.currentExtent;
  if (surfaceCapabilities.currentExtent.width == UINT32_MAX) {
    selectedSwapExtent = {
        .width = math::clamp<uint32_t>(mConfig.width, surfaceCapabilities.minImageExtent.width,
                                       surfaceCapabilities.maxImageExtent.width),
        .height = math::clamp<uint32_t>(mConfig.height, surfaceCapabilities.minImageExtent.height,
                                        surfaceCapabilities.maxImageExtent.height),
    };
  }

  // Prefer triple buffering
  uint32_t minImageCount = math::max(3u, surfaceCapabilities.minImageCount);
  if (surfaceCapabilities.maxImageCount > 0 && minImageCount > surfaceCapabilities.maxImageCount) {
    minImageCount = surfaceCapabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR swapchainCreateInfo{};
  swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainCreateInfo.surface = mConfig.surface;
  swapchainCreateInfo.minImageCount = minImageCount;
  swapchainCreateInfo.imageFormat = selectedSurfaceFormat.format;
  swapchainCreateInfo.imageColorSpace = selectedSurfaceFormat.colorSpace;
  swapchainCreateInfo.imageExtent = selectedSwapExtent;
  swapchainCreateInfo.imageArrayLayers = 1;
  swapchainCreateInfo.imageUsage = mConfig.imageUsage;
  swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
  swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapchainCreateInfo.presentMode = selectedPresentMode;
  swapchainCreateInfo.clipped = VK_TRUE;
  swapchainCreateInfo.oldSwapchain = iOldSwapchain;

  VK_CHECK(vkCreateSwapchainKHR(mDevice->getDevice(), &swapchainCreateInfo, nullptr, &mSwapchain));
  vk_utils::setDebugObjectName(mDevice->getDevice(), mSwapchain, "Main_Swapchain");

  mSurfaceFormat = selectedSurfaceFormat;
  mExtent = selectedSwapExtent;
  mPresentMode = selectedPresentMode;

  for (ImageResource& image : mImages) {
    if (image.view != VK_NULL_HANDLE) {
      vkDestroyImageView(mDevice->getDevice(), image.view, nullptr);
    }
    if (image.renderFinishedSemaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(mDevice->getDevice(), image.renderFinishedSemaphore, nullptr);
    }
  }
  mImages.clear();

  uint32_t swapchainImagesCount = 0;
  VK_CHECK(vkGetSwapchainImagesKHR(mDevice->getDevice(), mSwapchain, &swapchainImagesCount, nullptr));
  std::vector<VkImage> swapChainImages(swapchainImagesCount);
  VK_CHECK(vkGetSwapchainImagesKHR(mDevice->getDevice(), mSwapchain, &swapchainImagesCount, swapChainImages.data()));

  mImages.resize(swapChainImages.size());
  for (size_t i = 0; i < swapChainImages.size(); i++) {
    mImages[i].image = swapChainImages[i];

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = swapChainImages[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = mSurfaceFormat.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(mDevice->getDevice(), &viewInfo, nullptr, &mImages[i].view));

    VkSemaphoreCreateInfo semaphoreCreateInfo{};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(mDevice->getDevice(), &semaphoreCreateInfo, nullptr, &mImages[i].renderFinishedSemaphore));

    vk_utils::setDebugObjectName(mDevice->getDevice(), mImages[i].image, std::format("Swapchain_Image_{}", i).c_str());
    vk_utils::setDebugObjectName(mDevice->getDevice(), mImages[i].view, std::format("Swapchain_ImageView_{}", i).c_str());
    vk_utils::setDebugObjectName(mDevice->getDevice(), mImages[i].renderFinishedSemaphore,
                                 std::format("RenderFinished_Semaphore_{}", i).c_str());
  }

  NE_LOG("Created new swapChain, Present mode: {}, Image count: {}, Image size: {} x {}",
         mPresentMode == VK_PRESENT_MODE_FIFO_KHR ? "V-Sync" : "Mailbox", mImages.size(), mExtent.width, mExtent.height);
}

VkResult Swapchain::acquireNextImage(VkSemaphore iPresentCompleteSemaphore, uint32_t* oImageIndex, uint64_t iTimeout) {
  NE_ASSERT(oImageIndex != nullptr, "Output image index must not be null");
  return vkAcquireNextImageKHR(mDevice->getDevice(), mSwapchain, iTimeout, iPresentCompleteSemaphore, VK_NULL_HANDLE,
                               oImageIndex);
}

VkResult Swapchain::present(VkQueue iQueue, uint32_t iImageIndex, VkSemaphore iWaitSemaphore) {
  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &iWaitSemaphore;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &mSwapchain;
  presentInfo.pImageIndices = &iImageIndex;

  return vkQueuePresentKHR(iQueue, &presentInfo);
}

void Swapchain::recreate(uint32_t iWidth, uint32_t iHeight) {
  NE_ASSERT(iWidth > 0 && iHeight > 0, "Swapchain recreation dimensions must be positive");

  mConfig.width = iWidth;
  mConfig.height = iHeight;

  mDevice->waitIdle();
  NE_LOG("Recreating SwapChain... New resolution: {}x{}", iWidth, iHeight);

  VkSwapchainKHR oldSwapchain = mSwapchain;
  mSwapchain = VK_NULL_HANDLE;

  createSwapchain(oldSwapchain);

  if (oldSwapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(mDevice->getDevice(), oldSwapchain, nullptr);
  }
}

} // namespace ne
