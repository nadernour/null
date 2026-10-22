#pragma once

#include "renderer/buffer.h"

#include <memory>
#include <string>
#include <vector>
#include <volk/volk.h>

namespace ne {

class Device;

// Host-visible memory the GPU reads during one frame: per-draw data, uniforms, indirect commands. Uploads fill blocks in
// order, and when one is full the next block takes over, created the first time it's needed and kept, so nothing has to
// be sized up front. FrameManager keeps one per frame in flight and resets it once that frame's fence has signaled.
class UploadAllocator {
public:
  struct Allocation {
    VkBuffer buffer = VK_NULL_HANDLE; // With offset, for commands that read a buffer, such as indirect draws
    VkDeviceSize offset = 0;
    VkDeviceAddress address = 0;
  };

  UploadAllocator(Device* iDevice, VkDeviceSize iBlockSize, std::string iDebugName);
  ~UploadAllocator();

  UploadAllocator(const UploadAllocator&) = delete;
  UploadAllocator& operator=(const UploadAllocator&) = delete;
  UploadAllocator(UploadAllocator&&) = delete;
  UploadAllocator& operator=(UploadAllocator&&) = delete;

  // An upload larger than a block gets a block of its own size
  Allocation upload(const void* iData, VkDeviceSize iSize, VkDeviceSize iAlignment = Buffer::kDefaultAlignment);
  // Makes every block reusable. The GPU must be done reading them.
  void reset();

private:
  std::unique_ptr<Buffer> createBlock(VkDeviceSize iSize);

  Device* mDevice = nullptr;
  VkDeviceSize mBlockSize = 0;
  std::string mDebugName;
  std::vector<std::unique_ptr<Buffer>> mBlocks;
  size_t mCurrentBlock = 0; // Uploads go here or to a later block, never back to an earlier one
};

} // namespace ne
