#include "renderer/buffer.h"
#include "core/assert.h"
#include "core/logger.h"
#include "renderer/device.h"
#include "renderer/memory_allocator.h"
#include "renderer/utils.h"

// std
#include <cstring>
#include <string>
#include <vector>

namespace ne {

namespace {

[[maybe_unused]] std::string memoryUsageToString(MemoryUsage iUsage) {
  switch (iUsage) {
    case MemoryUsage::DeviceLocal:
      return "DeviceLocal";
    case MemoryUsage::Upload:
      return "Upload";
    case MemoryUsage::Readback:
      return "Readback";
  }
  return "Unknown";
}

[[maybe_unused]] std::string bufferUsageToString(VkBufferUsageFlags iUsage) {
  std::vector<std::string> flags;
  if (iUsage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
    flags.push_back("TRANSFER_SRC");
  if (iUsage & VK_BUFFER_USAGE_TRANSFER_DST_BIT)
    flags.push_back("TRANSFER_DST");
  if (iUsage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)
    flags.push_back("UNIFORM_TEXEL");
  if (iUsage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)
    flags.push_back("STORAGE_TEXEL");
  if (iUsage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
    flags.push_back("UNIFORM");
  if (iUsage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
    flags.push_back("STORAGE");
  if (iUsage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
    flags.push_back("INDEX");
  if (iUsage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
    flags.push_back("VERTEX");
  if (iUsage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
    flags.push_back("INDIRECT");
  if (iUsage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    flags.push_back("SHADER_DEVICE_ADDRESS");
  if (flags.empty())
    return "NONE";
  std::string result;
  for (size_t i = 0; i < flags.size(); ++i) {
    if (i > 0)
      result += " | ";
    result += flags[i];
  }
  return result;
}

[[maybe_unused]] std::string memoryPropertiesToString(VkMemoryPropertyFlags iProperties) {
  std::vector<std::string> flags;
  if (iProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    flags.push_back("DEVICE_LOCAL");
  if (iProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    flags.push_back("HOST_VISIBLE");
  if (iProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    flags.push_back("HOST_COHERENT");
  if (iProperties & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
    flags.push_back("HOST_CACHED");
  if (iProperties & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
    flags.push_back("LAZILY_ALLOCATED");
  if (iProperties & VK_MEMORY_PROPERTY_PROTECTED_BIT)
    flags.push_back("PROTECTED");
  if (flags.empty())
    return "NONE";
  std::string result;
  for (size_t i = 0; i < flags.size(); ++i) {
    if (i > 0)
      result += " | ";
    result += flags[i];
  }
  return result;
}

} // namespace

Buffer::Buffer(Device* iDevice, const Config& iConfig) : mDevice(iDevice), mConfig(iConfig) {
  NE_ASSERT(mConfig.size > 0, "Buffer size must be greater than 0");

  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = mConfig.size;
  bufferInfo.usage = mConfig.usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  const MemoryAllocator::BufferAllocation allocation =
      mDevice->getMemoryAllocator()->createBuffer(bufferInfo, mConfig.memoryUsage, mConfig.debugName);
  mBuffer = allocation.buffer;
  mAllocation = allocation.allocation;
  mMapped = allocation.mapped;

  if (mConfig.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = mBuffer;
    mDeviceAddress = vkGetBufferDeviceAddress(mDevice->getDevice(), &addressInfo);
  }

  if (!mConfig.debugName.empty()) {
    vk_utils::setDebugObjectName(mDevice->getDevice(), mBuffer, mConfig.debugName);
  }

  NE_LOG("Allocated Buffer{}: Size: {} (Allocated: {}) | Usage: [{}] | MemoryUsage: [{}] | Type: [{}]",
         mConfig.debugName.empty() ? "" : " '" + mConfig.debugName + "'", vk_utils::formatBytes(mConfig.size),
         vk_utils::formatBytes(allocation.allocatedSize), bufferUsageToString(mConfig.usage),
         memoryUsageToString(mConfig.memoryUsage), memoryPropertiesToString(allocation.memoryProperties));
}

Buffer::~Buffer() {
  NE_LOG("Destroyed Buffer{}: Size: {} | Usage: [{}]", mConfig.debugName.empty() ? "" : " '" + mConfig.debugName + "'",
         vk_utils::formatBytes(mConfig.size), bufferUsageToString(mConfig.usage));
  mDevice->getMemoryAllocator()->destroyBuffer(mBuffer, mAllocation);
}

void Buffer::writeToBuffer(const void* iData, VkDeviceSize iSize, VkDeviceSize iOffset) {
  NE_ASSERT(mMapped, "Buffer is not mapped (only Upload and Readback buffers are host-accessible)!");
  VkDeviceSize writeSize = (iSize == VK_WHOLE_SIZE) ? mConfig.size - iOffset : iSize;
  NE_ASSERT(iOffset + writeSize <= mConfig.size, "Buffer write exceeds buffer size!");
  std::memcpy(static_cast<char*>(mMapped) + iOffset, iData, writeSize);
  mDevice->getMemoryAllocator()->flush(mAllocation, iOffset, writeSize);
}

bool Buffer::canUpload(VkDeviceSize iSize, VkDeviceSize iAlignment) const {
  const VkDeviceSize alignment = std::max(kDefaultAlignment, iAlignment);
  return vk_utils::alignUp(mUploadOffset, alignment) + iSize <= mConfig.size;
}

VkDeviceSize Buffer::suballocate(VkDeviceSize iSize, VkDeviceSize iAlignment) {
  NE_ASSERT(canUpload(iSize, iAlignment), "Buffer overflow! Increase buffer size.");

  const VkDeviceSize alignment = std::max(kDefaultAlignment, iAlignment);
  const VkDeviceSize allocatedOffset = vk_utils::alignUp(mUploadOffset, alignment);
  mUploadOffset = allocatedOffset + iSize;
  return allocatedOffset;
}

VkDeviceSize Buffer::upload(const void* iData, VkDeviceSize iSize, VkDeviceSize iAlignment) {
  NE_ASSERT(mMapped, "Buffer is not mapped (only Upload and Readback buffers are host-accessible)!");
  VkDeviceSize allocatedOffset = suballocate(iSize, iAlignment);
  writeToBuffer(iData, iSize, allocatedOffset);
  return allocatedOffset;
}

} // namespace ne
