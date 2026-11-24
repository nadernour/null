#pragma once

#include <memory>
#include <string>
#include <vector>
#include <volk/volk.h>

namespace ne {

class Device;
class Buffer;
class Image;

class StagingManager {
public:
  explicit StagingManager(Device* iDevice);
  ~StagingManager();

  StagingManager(const StagingManager&) = delete;
  StagingManager& operator=(const StagingManager&) = delete;
  StagingManager(StagingManager&&) = delete;
  StagingManager& operator=(StagingManager&&) = delete;

  void beginBatch();
  void stageBufferCopy(VkBuffer iDstBuffer, const void* iData, VkDeviceSize iSize, VkDeviceSize iDstOffset = 0);
  // iPixelData holds every mip level of iDstImage, tightly packed, largest first
  void stageImageUpload(Image& iDstImage, const void* iPixelData, VkDeviceSize iSize);
  void endBatch();

  bool hasPendingUploads() const { return !mPendingBufferCopies.empty() || !mPendingImageUploads.empty(); }
  bool isBatching() const { return mIsBatching; }

private:
  struct PendingBufferCopy {
    VkBuffer dstBuffer = VK_NULL_HANDLE;
    VkDeviceSize srcOffset = 0;
    VkDeviceSize dstOffset = 0;
    VkDeviceSize size = 0;
  };

  struct PendingImageUpload {
    Image* image = nullptr;
    VkDeviceSize srcOffset = 0;
  };

  VkDeviceSize stageData(const void* iData, VkDeviceSize iSize);
  void flushBatch();

  void recordBufferCopy(VkCommandBuffer iCmd, VkBuffer iSrcBuffer, VkBuffer iDstBuffer, VkDeviceSize iSize,
                        VkDeviceSize iSrcOffset = 0, VkDeviceSize iDstOffset = 0);
  void recordImageCopy(VkCommandBuffer iCmd, VkBuffer iSrcBuffer, const Image& iDstImage, VkDeviceSize iSrcOffset = 0);

  Device* mDevice = nullptr;
  std::unique_ptr<Buffer> mStagingBuffer;

  bool mIsBatching = false;
  std::vector<PendingBufferCopy> mPendingBufferCopies;
  std::vector<PendingImageUpload> mPendingImageUploads;
};

} // namespace ne
