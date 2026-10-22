#include "renderer/frame_manager.h"
#include "core/assert.h"
#include "core/logger.h"
#include "platform/window.h"
#include "renderer/device.h"
#include "renderer/swapchain.h"
#include "renderer/upload_allocator.h"
#include "renderer/utils.h"

// std
#include <algorithm>
#include <cstring>
#include <format>

namespace ne {

FrameManager::FrameManager(Device* iDevice, Window* iWindow) : mDevice(iDevice), mWindow(iWindow) {
  NE_ASSERT(mDevice && mWindow && mWindow->getSurface() != VK_NULL_HANDLE);
  mFrameBufferResizeCallbackId = mWindow->addFrameBufferResizeCallback([this](int32_t, int32_t) { mFrameBufferResized = true; });

  int32_t width = 0, height = 0;
  mWindow->getFrameBufferSize(&width, &height);

  Swapchain::Config swapchainConfig{
      .surface = mWindow->getSurface(),
      .width = static_cast<uint32_t>(width),
      .height = static_cast<uint32_t>(height),
  };
  mSwapchain = std::make_unique<Swapchain>(mDevice, swapchainConfig);

  createFramesResources();
}

FrameManager::~FrameManager() {
  NE_VERBOSE("Destroying Vulkan FrameManager and deallocating resources...");

  if (mWindow && mFrameBufferResizeCallbackId != 0)
    mWindow->removeFrameBufferResizeCallback(mFrameBufferResizeCallbackId);

  for (FrameResources& frame : mFrames) {
    vkDestroyCommandPool(mDevice->getDevice(), frame.commandPool, nullptr);
    vkDestroySemaphore(mDevice->getDevice(), frame.imageAcquiredSemaphore, nullptr);
    vkDestroyFence(mDevice->getDevice(), frame.drawFence, nullptr);
    frame.uploadAllocator.reset();
  }

  mSwapchain.reset();

  NE_VERBOSE("Vulkan FrameManager destroyed successfully.");
}

void FrameManager::createFramesResources() {
  NE_ASSERT(mFrames.empty());
  mFrames.resize(kMaxFramesInFlight);
  for (size_t i = 0; i < mFrames.size(); i++) {
    // One pool per frame: resetting the pool reclaims all of the frame's command memory at once
    VkCommandPoolCreateInfo commandPoolCreateInfo{};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.queueFamilyIndex = mDevice->getQueueFamilyIndex();
    VK_CHECK(vkCreateCommandPool(mDevice->getDevice(), &commandPoolCreateInfo, nullptr, &mFrames[i].commandPool));

    VkCommandBufferAllocateInfo commandBufferAllocateInfo{};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandPool = mFrames[i].commandPool;
    commandBufferAllocateInfo.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(mDevice->getDevice(), &commandBufferAllocateInfo, &mFrames[i].commandBuffer));

    VkSemaphoreCreateInfo semaphoreCreateInfo{};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(mDevice->getDevice(), &semaphoreCreateInfo, nullptr, &mFrames[i].imageAcquiredSemaphore));

    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(mDevice->getDevice(), &fenceCreateInfo, nullptr, &mFrames[i].drawFence));

    mFrames[i].uploadAllocator =
        std::make_unique<UploadAllocator>(mDevice, vk_utils::kUploadBlockSize, std::format("Upload_Frame_{}", i));

    vk_utils::setDebugObjectName(mDevice->getDevice(), mFrames[i].commandPool, std::format("Frame_CommandPool_{}", i).c_str());
    vk_utils::setDebugObjectName(mDevice->getDevice(), mFrames[i].commandBuffer,
                                 std::format("Frame_CommandBuffer_{}", i).c_str());
    vk_utils::setDebugObjectName(mDevice->getDevice(), mFrames[i].imageAcquiredSemaphore,
                                 std::format("ImageAcquired_Semaphore_{}", i).c_str());
    vk_utils::setDebugObjectName(mDevice->getDevice(), mFrames[i].drawFence, std::format("Draw_Fence_{}", i).c_str());
  }
}

VkCommandBuffer FrameManager::beginFrame() {
  auto& currentFrame = mFrames[mFrameIndex];

  VK_CHECK(vkWaitForFences(mDevice->getDevice(), 1, &currentFrame.drawFence, VK_TRUE, UINT64_MAX));

  VkResult result = mSwapchain->acquireNextImage(currentFrame.imageAcquiredSemaphore, &mActiveImageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapChain(true);
    return VK_NULL_HANDLE;
  }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    NE_ASSERT(false, "failed to acquire swap chain image!");
  }

  vkResetFences(mDevice->getDevice(), 1, &currentFrame.drawFence);

  vkResetCommandPool(mDevice->getDevice(), currentFrame.commandPool, 0);

  currentFrame.uploadAllocator->reset();

  VkCommandBufferBeginInfo commandBufferBeginInfo{};
  commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  VK_CHECK(vkBeginCommandBuffer(currentFrame.commandBuffer, &commandBufferBeginInfo));

  return currentFrame.commandBuffer;
}

void FrameManager::endFrame() {
  auto& currentFrame = mFrames[mFrameIndex];
  VK_CHECK(vkEndCommandBuffer(currentFrame.commandBuffer));

  VkSemaphore renderFinishedSemaphore = mSwapchain->getImages()[mActiveImageIndex].renderFinishedSemaphore;

  VkSemaphoreSubmitInfo waitSemaphoreInfo{};
  waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  waitSemaphoreInfo.semaphore = currentFrame.imageAcquiredSemaphore;
  waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

  VkCommandBufferSubmitInfo commandBufferInfo{};
  commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  commandBufferInfo.commandBuffer = currentFrame.commandBuffer;

  VkSemaphoreSubmitInfo signalSemaphoreInfo{};
  signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  signalSemaphoreInfo.semaphore = renderFinishedSemaphore;
  signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

  VkSubmitInfo2 submitInfo2{};
  submitInfo2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  submitInfo2.pNext = NULL;
  submitInfo2.flags = 0;
  submitInfo2.waitSemaphoreInfoCount = 1;
  submitInfo2.pWaitSemaphoreInfos = &waitSemaphoreInfo;
  submitInfo2.commandBufferInfoCount = 1;
  submitInfo2.pCommandBufferInfos = &commandBufferInfo;
  submitInfo2.signalSemaphoreInfoCount = 1;
  submitInfo2.pSignalSemaphoreInfos = &signalSemaphoreInfo;
  VK_CHECK(vkQueueSubmit2(mDevice->getQueue(), 1, &submitInfo2, currentFrame.drawFence));

  VkResult result = mSwapchain->present(mDevice->getQueue(), mActiveImageIndex, renderFinishedSemaphore);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    recreateSwapChain(true);
  } else if (mFrameBufferResized) {
    mFrameBufferResized = false;
    recreateSwapChain(false);
  } else if (result != VK_SUCCESS) {
    NE_ASSERT(false, "failed to present swap chain image!");
  }

  mFrameIndex = (mFrameIndex + 1) % kMaxFramesInFlight;
}

void FrameManager::recreateSwapChain(bool iForce) {
  int32_t width = 0, height = 0;
  mWindow->getFrameBufferSize(&width, &height);
  while (width == 0 || height == 0) {
    mWindow->waitEvents();
    mWindow->getFrameBufferSize(&width, &height);
  }

  if (!iForce && static_cast<uint32_t>(width) == mSwapchain->getExtent().width &&
      static_cast<uint32_t>(height) == mSwapchain->getExtent().height) {
    return;
  }

  mSwapchain->recreate(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

VkFormat FrameManager::getColorFormat() const { return mSwapchain->getSurfaceFormat().format; }

uint32_t FrameManager::getImageCount() const { return static_cast<uint32_t>(mSwapchain->getImages().size()); }

VkExtent2D FrameManager::getExtent() const { return mSwapchain->getExtent(); }

VkImage FrameManager::getActiveImage() const { return mSwapchain->getImages()[mActiveImageIndex].image; }

VkImageView FrameManager::getActiveImageView() const { return mSwapchain->getImages()[mActiveImageIndex].view; }

} // namespace ne
