#pragma once

#include "renderer/rhi/rhi_types.h"

#include <string>
#include <volk/volk.h>

VK_DEFINE_HANDLE(VmaAllocator)

namespace ne {

class Instance;
class Device;

// The only code that includes or calls VMA
class MemoryAllocator {
public:
  struct BufferAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    MemoryAllocation allocation = nullptr;
    void* mapped = nullptr; // Non-null for host-visible usages (Upload, Readback)
    VkDeviceSize allocatedSize = 0;
    VkMemoryPropertyFlags memoryProperties = 0;
  };

  struct ImageAllocation {
    VkImage image = VK_NULL_HANDLE;
    MemoryAllocation allocation = nullptr;
  };

  MemoryAllocator(Instance* iInstance, Device* iDevice);
  ~MemoryAllocator();

  MemoryAllocator(const MemoryAllocator&) = delete;
  MemoryAllocator& operator=(const MemoryAllocator&) = delete;
  MemoryAllocator(MemoryAllocator&&) = delete;
  MemoryAllocator& operator=(MemoryAllocator&&) = delete;

  BufferAllocation createBuffer(const VkBufferCreateInfo& iCreateInfo, MemoryUsage iUsage, const std::string& iDebugName);
  ImageAllocation createImage(const VkImageCreateInfo& iCreateInfo, const std::string& iDebugName);
  void destroyBuffer(VkBuffer iBuffer, MemoryAllocation iAllocation);
  void destroyImage(VkImage iImage, MemoryAllocation iAllocation);

  // Makes host writes visible to the device; a no-op on host-coherent memory.
  void flush(MemoryAllocation iAllocation, VkDeviceSize iOffset, VkDeviceSize iSize);

private:
  VmaAllocator mAllocator = VK_NULL_HANDLE;
};

} // namespace ne
