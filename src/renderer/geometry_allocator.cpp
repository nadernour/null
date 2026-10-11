#include "renderer/geometry_allocator.h"
#include "core/assert.h"
#include "core/mesh_data.h"
#include "renderer/buffer.h"
#include "renderer/device.h"
#include "renderer/gpu_types.h"
#include "renderer/staging_manager.h"
#include "renderer/utils.h"

namespace ne {

GeometryAllocator::GeometryAllocator(Device* iDevice, VkDeviceSize iVertexPoolSize, VkDeviceSize iIndexPoolSize) {
  NE_ASSERT(iDevice, "Device must not be null");

  Buffer::Config vertexConfig{
      .size = iVertexPoolSize,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      .memoryUsage = MemoryUsage::DeviceLocal,
      .debugName = "GeometryAllocator_VertexBuffer",
  };
  mVertexBuffer = std::make_unique<Buffer>(iDevice, vertexConfig);

  Buffer::Config indexConfig{
      .size = iIndexPoolSize,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      .memoryUsage = MemoryUsage::DeviceLocal,
      .debugName = "GeometryAllocator_IndexBuffer",
  };
  mIndexBuffer = std::make_unique<Buffer>(iDevice, indexConfig);

  NE_LOG("Initialized GeometryAllocator: Vertex pool size: {}, Index pool size: {}", vk_utils::formatBytes(iVertexPoolSize),
         vk_utils::formatBytes(iIndexPoolSize));
}

GeometryAllocator::~GeometryAllocator() = default;

Mesh::Submesh GeometryAllocator::stageSubmesh(StagingManager& iStagingManager, const SubmeshData& iSubmeshData) {
  NE_ASSERT(!iSubmeshData.mPositions.empty(), "Mesh positions cannot be empty");
  NE_ASSERT(!iSubmeshData.mIndices.empty(), "Mesh indices cannot be empty");

  const size_t vertexCount = iSubmeshData.mPositions.size();
  const bool hasNormals = (iSubmeshData.mNormals.size() == vertexCount);
  const bool hasTexCoords = (iSubmeshData.mTexCoords.size() == vertexCount);
  const bool hasColors = (iSubmeshData.mColors.size() == vertexCount);

  std::vector<gpu::Vertex> vertices(vertexCount);
  for (size_t i = 0; i < vertexCount; ++i) {
    vertices[i].pos = iSubmeshData.mPositions[i];
    vertices[i].normal = hasNormals ? iSubmeshData.mNormals[i] : Vec3(0.0f, 0.0f, 1.0f);
    vertices[i].uv = hasTexCoords ? iSubmeshData.mTexCoords[i] : Vec2(0.0f, 0.0f);
    vertices[i].color = hasColors ? iSubmeshData.mColors[i] : LinearColor::White;
  }

  VkDeviceSize vertexSize = vertices.size() * sizeof(gpu::Vertex);
  VkDeviceSize indexSize = iSubmeshData.mIndices.size() * sizeof(uint32_t);

  VkDeviceSize vertexOffset = mVertexBuffer->suballocate(vertexSize);
  VkDeviceSize indexOffset = mIndexBuffer->suballocate(indexSize);

  NE_LOG("Allocated geometry: vertex size: {}, index size: {} | Pool occupancy: vertex={}/{} ({:.2f}%), index={}/{} ({:.2f}%)",
         vk_utils::formatBytes(vertexSize), vk_utils::formatBytes(indexSize),
         vk_utils::formatBytes(mVertexBuffer->getUploadOffset()), vk_utils::formatBytes(mVertexBuffer->getConfig().size),
         (static_cast<double>(mVertexBuffer->getUploadOffset()) / mVertexBuffer->getConfig().size) * 100.0,
         vk_utils::formatBytes(mIndexBuffer->getUploadOffset()), vk_utils::formatBytes(mIndexBuffer->getConfig().size),
         (static_cast<double>(mIndexBuffer->getUploadOffset()) / mIndexBuffer->getConfig().size) * 100.0);

  iStagingManager.stageBufferCopy(mVertexBuffer->getBuffer(), vertices.data(), vertexSize, vertexOffset);
  iStagingManager.stageBufferCopy(mIndexBuffer->getBuffer(), iSubmeshData.mIndices.data(), indexSize, indexOffset);

  return Mesh::Submesh{
      .mVertexAddress = mVertexBuffer->getDeviceAddress(vertexOffset),
      .mFirstIndex = static_cast<uint32_t>(indexOffset / sizeof(uint32_t)),
      .mIndexCount = static_cast<uint32_t>(iSubmeshData.mIndices.size()),
  };
}

void GeometryAllocator::bindIndexBuffer(VkCommandBuffer iCommandBuffer) const {
  NE_ASSERT(iCommandBuffer != VK_NULL_HANDLE, "Command buffer must not be null");
  NE_ASSERT(mIndexBuffer, "Index buffer must not be null");
  vkCmdBindIndexBuffer(iCommandBuffer, mIndexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
}

} // namespace ne
