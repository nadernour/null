#pragma once

#include "renderer/rhi_types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <volk/volk.h>

namespace ne {

class Device;
struct ImageData;

class Image {
public:
  struct Config {
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    uint32_t mipLevels = 1;
    VkImageAspectFlags aspectMask = 0; // 0 = auto-deduce from format
    std::string debugName = "";
  };

  Image(Device* iDevice, const Config& iConfig);
  ~Image();

  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;
  Image(Image&&) = delete;
  Image& operator=(Image&&) = delete;

  // The barrier from the tracked state to the given one, which becomes the tracked state. The caller records it before
  // the image's next use, alone or batched with others. Earlier submissions count too: an image shared by frames in
  // flight waits for the previous frame's last use.
  VkImageMemoryBarrier2 transitionBarrier(VkImageLayout iNewLayout, VkAccessFlags2 iDstAccessMask,
                                          VkPipelineStageFlags2 iDstStageMask);
  // Records transitionBarrier's barrier on its own
  void transition(VkCommandBuffer iCommandBuffer, VkImageLayout iNewLayout, VkAccessFlags2 iDstAccessMask,
                  VkPipelineStageFlags2 iDstStageMask);

  bool isValid() const { return mImage != VK_NULL_HANDLE; }
  VkImage getImage() const { return mImage; }
  VkImageView getImageView() const { return mImageView; }
  const Config& getConfig() const { return mConfig; }
  const VkImageSubresourceRange& getSubresourceRange() const { return mSubresourceRange; }

private:
  Device* mDevice = nullptr;
  Config mConfig;

  VkImage mImage = VK_NULL_HANDLE;
  MemoryAllocation mAllocation = nullptr;
  VkImageView mImageView = VK_NULL_HANDLE;
  VkImageSubresourceRange mSubresourceRange{};

  // Last layout, access, and stage recorded on this image; its next barrier starts from them
  VkImageLayout mCurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkAccessFlags2 mCurrentAccessMask = VK_ACCESS_2_NONE;
  VkPipelineStageFlags2 mCurrentStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
};

} // namespace ne
