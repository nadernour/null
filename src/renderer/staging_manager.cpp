#include "renderer/staging_manager.h"
#include "core/assert.h"
#include "core/logger.h"
#include "core/math/math_utils.h"
#include "renderer/buffer.h"
#include "renderer/device.h"
#include "renderer/image.h"
#include "renderer/utils.h"

#include <vulkan/utility/vk_format_utils.h>

#include <cstring>

namespace ne {

namespace {

// Tightly packed bytes of one mip level
VkDeviceSize getLevelSize(const Image::Config& iConfig, uint32_t iLevel) {
  return static_cast<VkDeviceSize>(math::max(1u, iConfig.width >> iLevel)) * math::max(1u, iConfig.height >> iLevel) *
         vkuFormatTexelBlockSize(iConfig.format);
}

} // namespace

StagingManager::StagingManager(Device* iDevice) : mDevice(iDevice) {
  NE_ASSERT(mDevice, "Device must not be null");

  Buffer::Config stagingConfig{
      .size = vk_utils::kStagingBufferSize,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .memoryUsage = MemoryUsage::Upload,
      .debugName = "StagingManager_StagingBuffer",
  };
  mStagingBuffer = std::make_unique<Buffer>(mDevice, stagingConfig);

  NE_VERBOSE("Initialized StagingManager: Staging Arena Size: {}", vk_utils::formatBytes(mStagingBuffer->getConfig().size));
}

StagingManager::~StagingManager() {
  if (mIsBatching) {
    endBatch();
  }
  mStagingBuffer.reset();
}

void StagingManager::beginBatch() {
  NE_ASSERT(!mIsBatching, "StagingManager: beginBatch() called while already in a batch!");
  mIsBatching = true;
  mPendingBufferCopies.clear();
  mPendingImageUploads.clear();
}

VkDeviceSize StagingManager::stageData(const void* iData, VkDeviceSize iSize) {
  NE_ASSERT(iData && iSize > 0, "Invalid data or size for staging");

  // Flush the pending batch when it is full
  if (!mStagingBuffer->canUpload(iSize)) {
    NE_VERBOSE("StagingManager: Staging capacity reached. Auto-flushing batch...");
    flushBatch();
  }

  NE_ASSERT(mStagingBuffer->canUpload(iSize), "Allocation still exceeds staging buffer capacity after flush!");

  return mStagingBuffer->upload(iData, iSize);
}

void StagingManager::stageBufferCopy(VkBuffer iDstBuffer, const void* iData, VkDeviceSize iSize, VkDeviceSize iDstOffset) {
  NE_ASSERT(iDstBuffer != VK_NULL_HANDLE, "Destination buffer must not be null");
  if (!iData || iSize == 0) {
    return;
  }

  // Larger than the whole staging buffer: upload through a temporary buffer
  if (iSize > mStagingBuffer->getConfig().size) {
    NE_VERBOSE("StagingManager: Staging buffer copy of size {} exceeds capacity {}. Using transient staging buffer.",
               vk_utils::formatBytes(iSize), vk_utils::formatBytes(mStagingBuffer->getConfig().size));
    if (hasPendingUploads()) {
      flushBatch();
    }

    Buffer::Config outlierConfig{
        .size = iSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .memoryUsage = MemoryUsage::Upload,
        .debugName = "StagingManager_OutlierBufferStaging",
    };
    Buffer tempStaging(mDevice, outlierConfig);
    tempStaging.writeToBuffer(iData, iSize, 0);

    VkCommandBuffer cmd = mDevice->beginOneTimeCommand();
    recordBufferCopy(cmd, tempStaging.getBuffer(), iDstBuffer, iSize, 0, iDstOffset);
    mDevice->endOneTimeCommand(cmd);
    return;
  }

  VkDeviceSize srcOffset = stageData(iData, iSize);
  mPendingBufferCopies.push_back(PendingBufferCopy{
      .dstBuffer = iDstBuffer,
      .srcOffset = srcOffset,
      .dstOffset = iDstOffset,
      .size = iSize,
  });
}

void StagingManager::stageImageUpload(Image& iDstImage, const void* iPixelData, VkDeviceSize iSize) {
  NE_ASSERT(iDstImage.isValid(), "Destination image must be valid");
  if (!iPixelData || iSize == 0) {
    return;
  }

  VkDeviceSize chainSize = 0;
  for (uint32_t level = 0; level < iDstImage.getConfig().mipLevels; ++level) {
    chainSize += getLevelSize(iDstImage.getConfig(), level);
  }
  NE_ASSERT(iSize == chainSize, "Image upload of {} bytes must hold all {} mip levels ({} bytes)", iSize,
            iDstImage.getConfig().mipLevels, chainSize);

  // Larger than the whole staging buffer: upload through a temporary buffer
  if (iSize > mStagingBuffer->getConfig().size) {
    NE_VERBOSE("StagingManager: Staging image upload of size {} exceeds capacity {}. Using transient staging buffer.",
               vk_utils::formatBytes(iSize), vk_utils::formatBytes(mStagingBuffer->getConfig().size));
    if (hasPendingUploads()) {
      flushBatch();
    }

    Buffer::Config outlierConfig{
        .size = iSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .memoryUsage = MemoryUsage::Upload,
        .debugName = "StagingManager_OutlierImageStaging",
    };
    Buffer tempStaging(mDevice, outlierConfig);
    tempStaging.writeToBuffer(iPixelData, iSize, 0);

    VkCommandBuffer cmd = mDevice->beginOneTimeCommand();
    iDstImage.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT);
    recordImageCopy(cmd, tempStaging.getBuffer(), iDstImage);
    iDstImage.transition(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    mDevice->endOneTimeCommand(cmd);
    return;
  }

  VkDeviceSize srcOffset = stageData(iPixelData, iSize);
  mPendingImageUploads.push_back(PendingImageUpload{
      .image = &iDstImage,
      .srcOffset = srcOffset,
  });
}

void StagingManager::flushBatch() {
  if (!hasPendingUploads()) {
    return;
  }

  VkCommandBuffer cmd = mDevice->beginOneTimeCommand();

  // One barrier batch before all copies and one after
  std::vector<VkImageMemoryBarrier2> barriers;
  barriers.reserve(mPendingImageUploads.size());
  for (const auto& item : mPendingImageUploads) {
    barriers.push_back(item.image->transitionBarrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                     VK_PIPELINE_STAGE_2_COPY_BIT));
  }
  vk_utils::pipelineBarrier(cmd, barriers);

  for (const auto& item : mPendingImageUploads) {
    recordImageCopy(cmd, mStagingBuffer->getBuffer(), *item.image, item.srcOffset);
  }

  for (const auto& copy : mPendingBufferCopies) {
    recordBufferCopy(cmd, mStagingBuffer->getBuffer(), copy.dstBuffer, copy.size, copy.srcOffset, copy.dstOffset);
  }

  barriers.clear();
  for (const auto& item : mPendingImageUploads) {
    barriers.push_back(item.image->transitionBarrier(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_SHADER_READ_BIT,
                                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT));
  }
  vk_utils::pipelineBarrier(cmd, barriers);

  mDevice->endOneTimeCommand(cmd);

  mPendingBufferCopies.clear();
  mPendingImageUploads.clear();
  mStagingBuffer->resetUploadOffset();
}

void StagingManager::endBatch() {
  if (!mIsBatching && !hasPendingUploads()) {
    return;
  }
  flushBatch();
  mIsBatching = false;
}

void StagingManager::recordBufferCopy(VkCommandBuffer iCmd, VkBuffer iSrcBuffer, VkBuffer iDstBuffer, VkDeviceSize iSize,
                                      VkDeviceSize iSrcOffset, VkDeviceSize iDstOffset) {
  VkBufferCopy2 region{};
  region.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
  region.srcOffset = iSrcOffset;
  region.dstOffset = iDstOffset;
  region.size = iSize;

  VkCopyBufferInfo2 copyInfo{};
  copyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
  copyInfo.srcBuffer = iSrcBuffer;
  copyInfo.dstBuffer = iDstBuffer;
  copyInfo.regionCount = 1;
  copyInfo.pRegions = &region;

  vkCmdCopyBuffer2(iCmd, &copyInfo);
}

void StagingManager::recordImageCopy(VkCommandBuffer iCmd, VkBuffer iSrcBuffer, const Image& iDstImage, VkDeviceSize iSrcOffset) {
  const Image::Config& config = iDstImage.getConfig();

  // One region per mip level, read back to back from the staging buffer
  std::vector<VkBufferImageCopy2> copyRegions(config.mipLevels);
  VkDeviceSize levelOffset = iSrcOffset;
  for (uint32_t level = 0; level < config.mipLevels; ++level) {
    VkBufferImageCopy2& copyRegion = copyRegions[level];
    copyRegion.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
    copyRegion.bufferOffset = levelOffset;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = level;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = {0, 0, 0};
    copyRegion.imageExtent = {math::max(1u, config.width >> level), math::max(1u, config.height >> level), 1};
    levelOffset += getLevelSize(config, level);
  }

  VkCopyBufferToImageInfo2 copyInfo{};
  copyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
  copyInfo.srcBuffer = iSrcBuffer;
  copyInfo.dstImage = iDstImage.getImage();
  copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  copyInfo.regionCount = static_cast<uint32_t>(copyRegions.size());
  copyInfo.pRegions = copyRegions.data();

  vkCmdCopyBufferToImage2(iCmd, &copyInfo);
}

} // namespace ne
