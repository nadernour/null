#pragma once

#include "renderer/rhi/rhi_types.h"

#include <string>
#include <volk/volk.h>

namespace ne {

class Device;

class Buffer {
public:
  static constexpr VkDeviceSize kDefaultAlignment = 16;

  struct Config {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    MemoryUsage memoryUsage = MemoryUsage::DeviceLocal;
    std::string debugName = "";
  };

  Buffer(Device* iDevice, const Config& iConfig);
  ~Buffer();

  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  Buffer(Buffer&&) = delete;
  Buffer& operator=(Buffer&&) = delete;

  void writeToBuffer(const void* iData, VkDeviceSize iSize = VK_WHOLE_SIZE, VkDeviceSize iOffset = 0);

  VkDeviceSize suballocate(VkDeviceSize iSize, VkDeviceSize iAlignment = kDefaultAlignment);
  VkDeviceSize upload(const void* iData, VkDeviceSize iSize, VkDeviceSize iAlignment = kDefaultAlignment);
  bool canUpload(VkDeviceSize iSize, VkDeviceSize iAlignment = kDefaultAlignment) const;
  void resetUploadOffset() { mUploadOffset = 0; }
  VkDeviceSize getUploadOffset() const { return mUploadOffset; }

  VkDeviceAddress getDeviceAddress(VkDeviceSize iOffset = 0) const {
    return mDeviceAddress != 0 ? (mDeviceAddress + iOffset) : 0;
  }

  const Config& getConfig() const { return mConfig; }
  VkBuffer getBuffer() const { return mBuffer; }
  void* getMappedData() const { return mMapped; }
  bool isMapped() const { return mMapped != nullptr; }

private:
  Device* mDevice = nullptr;

  Config mConfig;
  VkBuffer mBuffer = VK_NULL_HANDLE;
  MemoryAllocation mAllocation = nullptr;
  VkDeviceAddress mDeviceAddress = 0;
  void* mMapped = nullptr;
  VkDeviceSize mUploadOffset = 0;
};

} // namespace ne
