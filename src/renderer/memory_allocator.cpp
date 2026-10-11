#include <volk/volk.h>
#include <vma/vk_mem_alloc.h>

#include "core/assert.h"
#include "core/logger.h"
#include "renderer/device.h"
#include "renderer/instance.h"
#include "renderer/memory_allocator.h"
#include "renderer/utils.h"

namespace ne {

namespace {

VmaAllocation toVma(MemoryAllocation iAllocation) { return reinterpret_cast<VmaAllocation>(iAllocation); }
MemoryAllocation fromVma(VmaAllocation iAllocation) { return reinterpret_cast<MemoryAllocation>(iAllocation); }

VmaAllocationCreateInfo toAllocationCreateInfo(MemoryUsage iUsage) {
  VmaAllocationCreateInfo allocInfo{};
  switch (iUsage) {
    case MemoryUsage::DeviceLocal:
      allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
      break;
    case MemoryUsage::Upload:
      allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
      allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
      break;
    case MemoryUsage::Readback:
      allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
      allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
      break;
  }
  return allocInfo;
}

} // namespace

MemoryAllocator::MemoryAllocator(Instance* iInstance, Device* iDevice) {
  NE_ASSERT(iInstance, "Instance cannot be null for MemoryAllocator initialization");
  NE_ASSERT(iDevice, "Device cannot be null for MemoryAllocator initialization");

  VmaAllocatorCreateInfo allocatorCreateInfo{};
  allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_4;
  allocatorCreateInfo.physicalDevice = iDevice->getPhysicalDevice();
  allocatorCreateInfo.device = iDevice->getDevice();
  allocatorCreateInfo.instance = iInstance->getInstance();
  allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

  VmaVulkanFunctions vulkanFunctions{};
  vmaImportVulkanFunctionsFromVolk(&allocatorCreateInfo, &vulkanFunctions);
  allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;

  VK_CHECK(vmaCreateAllocator(&allocatorCreateInfo, &mAllocator));
  NE_LOG("MemoryAllocator (VMA) initialized successfully for Vulkan 1.4.");
}

MemoryAllocator::~MemoryAllocator() {
  vmaDestroyAllocator(mAllocator);
  NE_LOG("MemoryAllocator (VMA) destroyed successfully.");
}

MemoryAllocator::BufferAllocation MemoryAllocator::createBuffer(const VkBufferCreateInfo& iCreateInfo, MemoryUsage iUsage,
                                                                const std::string& iDebugName) {
  const VmaAllocationCreateInfo allocCreateInfo = toAllocationCreateInfo(iUsage);

  BufferAllocation result{};
  VmaAllocation allocation = VK_NULL_HANDLE;
  VmaAllocationInfo allocationInfo{};
  VK_CHECK(vmaCreateBuffer(mAllocator, &iCreateInfo, &allocCreateInfo, &result.buffer, &allocation, &allocationInfo));

  if (!iDebugName.empty()) {
    vmaSetAllocationName(mAllocator, allocation, iDebugName.c_str());
  }

  vmaGetAllocationMemoryProperties(mAllocator, allocation, &result.memoryProperties);
  result.allocation = fromVma(allocation);
  result.mapped = allocationInfo.pMappedData;
  result.allocatedSize = allocationInfo.size;
  return result;
}

MemoryAllocator::ImageAllocation MemoryAllocator::createImage(const VkImageCreateInfo& iCreateInfo, const std::string& iDebugName) {
  const VmaAllocationCreateInfo allocCreateInfo = toAllocationCreateInfo(MemoryUsage::DeviceLocal);

  ImageAllocation result{};
  VmaAllocation allocation = VK_NULL_HANDLE;
  VK_CHECK(vmaCreateImage(mAllocator, &iCreateInfo, &allocCreateInfo, &result.image, &allocation, nullptr));

  if (!iDebugName.empty()) {
    vmaSetAllocationName(mAllocator, allocation, iDebugName.c_str());
  }

  result.allocation = fromVma(allocation);
  return result;
}

void MemoryAllocator::destroyBuffer(VkBuffer iBuffer, MemoryAllocation iAllocation) {
  vmaDestroyBuffer(mAllocator, iBuffer, toVma(iAllocation));
}

void MemoryAllocator::destroyImage(VkImage iImage, MemoryAllocation iAllocation) {
  vmaDestroyImage(mAllocator, iImage, toVma(iAllocation));
}

void MemoryAllocator::flush(MemoryAllocation iAllocation, VkDeviceSize iOffset, VkDeviceSize iSize) {
  VK_CHECK(vmaFlushAllocation(mAllocator, toVma(iAllocation), iOffset, iSize));
}

} // namespace ne
