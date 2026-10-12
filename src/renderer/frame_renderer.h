#pragma once

#include "core/event.h"
#include <volk/volk.h>

// std lib headers
#include <memory>
#include <string>
#include <vector>

namespace ne {

class Window;
class Buffer;
class Image;
class Device;
class Swapchain;

class FrameRenderer {
public:
  FrameRenderer(Device* iDevice, Window* iWindow);
  ~FrameRenderer();

  FrameRenderer(const FrameRenderer&) = delete;
  FrameRenderer& operator=(const FrameRenderer&) = delete;
  FrameRenderer(FrameRenderer&&) = delete;
  FrameRenderer& operator=(FrameRenderer&&) = delete;

  VkCommandBuffer beginFrame();
  void endFrame();

  Image* getDepthImage() const { return mDepthImage.get(); }
  VkFormat getColorFormat() const;
  VkFormat getDepthFormat() const;
  uint32_t getImageCount() const;
  VkExtent2D getExtent() const;

  VkImage getActiveImage() const;
  VkImageView getActiveImageView() const;

  Buffer* getUploadBuffer() const { return mFrames[mFrameIndex].uploadBuffer.get(); }
  void recreateUploadBuffer(VkDeviceSize iNewSize);

private:
  std::unique_ptr<Buffer> createUploadBuffer(VkDeviceSize iSize, std::string iDebugName = "");

  void createDepthImage();
  void createFramesResources();

  void recreateSwapChain(bool iForce = false);

  static constexpr uint32_t kMaxFramesInFlight = 2; // Frames the CPU may record ahead of the GPU

  Device* mDevice = nullptr;
  Window* mWindow = nullptr;
  std::unique_ptr<Swapchain> mSwapchain;

  std::unique_ptr<Image> mDepthImage;

  struct FrameResources {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore presentCompleteSemaphore = VK_NULL_HANDLE;
    VkFence drawFence = VK_NULL_HANDLE;
    std::unique_ptr<Buffer> uploadBuffer;
  };
  std::vector<FrameResources> mFrames;

  uint32_t mFrameIndex = 0;
  uint32_t mActiveImageIndex = 0;

  bool mFrameBufferResized = false;
  CallbackId mFrameBufferResizeCallbackId = 0;
};

} // namespace ne
