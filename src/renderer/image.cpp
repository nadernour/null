#include "renderer/image.h"
#include "core/assert.h"
#include "core/logger.h"
#include "renderer/device.h"
#include "renderer/memory_allocator.h"
#include "renderer/utils.h"

namespace ne {

Image::Image(Device* iDevice, const Config& iConfig) : mDevice(iDevice), mConfig(iConfig) {
  NE_ASSERT(mConfig.width > 0 && mConfig.height > 0, "Image dimensions must be greater than 0");

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = mConfig.width;
  imageInfo.extent.height = mConfig.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = mConfig.mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.format = mConfig.format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = mConfig.usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  const MemoryAllocator::ImageAllocation allocation = mDevice->getMemoryAllocator()->createImage(imageInfo, mConfig.debugName);
  mImage = allocation.image;
  mAllocation = allocation.allocation;

  VkImageAspectFlags aspectMask = mConfig.aspectMask != 0 ? mConfig.aspectMask : vk_utils::deduceAspectFlags(mConfig.format);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = mImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = mConfig.format;
  viewInfo.subresourceRange.aspectMask = aspectMask;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = mConfig.mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;
  mSubresourceRange = viewInfo.subresourceRange;

  VK_CHECK(vkCreateImageView(mDevice->getDevice(), &viewInfo, nullptr, &mImageView));

  mCurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  mCurrentAccessMask = VK_ACCESS_2_NONE;
  mCurrentStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

  if (!mConfig.debugName.empty()) {
    vk_utils::setDebugObjectName(mDevice->getDevice(), mImage, mConfig.debugName);
    vk_utils::setDebugObjectName(mDevice->getDevice(), mImageView, mConfig.debugName + "_View");
  }

  NE_VERBOSE("Allocated Image{}: Extent {}x{} | Format: {}",
             mConfig.debugName.empty() ? "" : std::format(" '{}'", mConfig.debugName), mConfig.width, mConfig.height,
             string_VkFormat(mConfig.format));
}

Image::~Image() {
  vkDestroyImageView(mDevice->getDevice(), mImageView, nullptr);
  mDevice->getMemoryAllocator()->destroyImage(mImage, mAllocation);
}

} // namespace ne
