#include "renderer/upload_allocator.h"
#include "core/assert.h"
#include "core/math/math_utils.h"
#include "renderer/device.h"

// std
#include <format>

namespace ne {

UploadAllocator::UploadAllocator(Device* iDevice, VkDeviceSize iBlockSize, std::string iDebugName)
    : mDevice(iDevice), mBlockSize(iBlockSize), mDebugName(std::move(iDebugName)) {
  NE_ASSERT(mDevice && mBlockSize > 0);
  // Created up front, so a frame whose uploads fit in one block never allocates
  mBlocks.push_back(createBlock(mBlockSize));
}

UploadAllocator::~UploadAllocator() = default;

UploadAllocator::Allocation UploadAllocator::upload(const void* iData, VkDeviceSize iSize, VkDeviceSize iAlignment) {
  while (mCurrentBlock < mBlocks.size() && !mBlocks[mCurrentBlock]->canUpload(iSize, iAlignment)) {
    ++mCurrentBlock;
  }
  if (mCurrentBlock == mBlocks.size()) {
    mBlocks.push_back(createBlock(math::max(mBlockSize, iSize)));
  }

  Buffer& block = *mBlocks[mCurrentBlock];
  const VkDeviceSize offset = block.upload(iData, iSize, iAlignment);
  return Allocation{.buffer = block.getBuffer(), .offset = offset, .address = block.getDeviceAddress(offset)};
}

void UploadAllocator::reset() {
  for (std::unique_ptr<Buffer>& block : mBlocks) {
    block->resetUploadOffset();
  }
  mCurrentBlock = 0;
}

std::unique_ptr<Buffer> UploadAllocator::createBlock(VkDeviceSize iSize) {
  Buffer::Config config{
      .size = iSize,
      .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
      .memoryUsage = MemoryUsage::Upload,
      .debugName = std::format("{}_Block_{}", mDebugName, mBlocks.size()),
  };
  return std::make_unique<Buffer>(mDevice, config);
}

} // namespace ne
