#pragma once

#include "core/event.h"
#include <volk/volk.h>

// std lib headers
#include <memory>
#include <vector>

namespace ne {

class Window;
class Device;
class Swapchain;
class UploadAllocator;

// The swapchain and the frames in flight, each with its command buffer, sync objects, and upload allocator
class FrameManager {
public:
  // The stage at which a frame's submit waits for its swapchain image; earlier stages may run while the display still
  // reads it. The frame's first barrier on the image uses it as its source stage, so the layout change waits for the display too.
  static constexpr VkPipelineStageFlags2 kImageAcquiredStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

  FrameManager(Device* iDevice, Window* iWindow);
  ~FrameManager();

  FrameManager(const FrameManager&) = delete;
  FrameManager& operator=(const FrameManager&) = delete;
  FrameManager(FrameManager&&) = delete;
  FrameManager& operator=(FrameManager&&) = delete;

  VkCommandBuffer beginFrame();
  void endFrame();

  VkFormat getColorFormat() const;
  uint32_t getImageCount() const;
  VkExtent2D getExtent() const;

  VkImage getActiveImage() const;
  VkImageView getActiveImageView() const;

  // The current frame's, between beginFrame and endFrame
  UploadAllocator* getUploadAllocator() const { return mFrames[mFrameIndex].uploadAllocator.get(); }

private:
  void createFramesResources();

  void recreateSwapChain(bool iForce = false);

  static constexpr uint32_t kMaxFramesInFlight = 2; // Frames the CPU may record ahead of the GPU

  Device* mDevice = nullptr;
  Window* mWindow = nullptr;
  std::unique_ptr<Swapchain> mSwapchain;

  struct FrameResources {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAcquiredSemaphore = VK_NULL_HANDLE;
    VkFence drawFence = VK_NULL_HANDLE;
    std::unique_ptr<UploadAllocator> uploadAllocator;
  };
  std::vector<FrameResources> mFrames;

  uint32_t mFrameIndex = 0;
  uint32_t mActiveImageIndex = 0;

  bool mFrameBufferResized = false;
  CallbackId mFrameBufferResizeCallbackId = 0;
};

} // namespace ne
