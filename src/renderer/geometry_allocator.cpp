#include "renderer/geometry_allocator.h"
#include "core/assert.h"
#include "core/logger.h"
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

  NE_VERBOSE("Initialized GeometryAllocator: Vertex pool size: {}, Index pool size: {}", vk_utils::formatBytes(iVertexPoolSize),
             vk_utils::formatBytes(iIndexPoolSize));
}

GeometryAllocator::~GeometryAllocator() = default;

std::optional<Mesh> GeometryAllocator::stageMesh(StagingManager& iStagingManager, const MeshData& iMeshData) {
  // The submeshes share one vertex and one index range, so the mesh fits exactly when both totals do
  size_t vertexCount = 0;
  size_t indexCount = 0;
  for (const SubmeshData& submesh : iMeshData.submeshes) {
    vertexCount += submesh.positions.size();
    indexCount += submesh.indices.size();
  }
  const VkDeviceSize vertexSize = vertexCount * sizeof(gpu::Vertex);
  const VkDeviceSize indexSize = indexCount * sizeof(uint32_t);
  if (!mVertexBuffer->canUpload(vertexSize) || !mIndexBuffer->canUpload(indexSize)) {
    NE_ERROR("GeometryAllocator: Mesh needs {} of vertex and {} of index pool space, but only {} and {} are left",
             vk_utils::formatBytes(vertexSize), vk_utils::formatBytes(indexSize),
             vk_utils::formatBytes(mVertexBuffer->getConfig().size - mVertexBuffer->getUploadOffset()),
             vk_utils::formatBytes(mIndexBuffer->getConfig().size - mIndexBuffer->getUploadOffset()));
    return std::nullopt;
  }

  const VkDeviceSize vertexOffset = mVertexBuffer->suballocate(vertexSize);
  const VkDeviceSize indexOffset = mIndexBuffer->suballocate(indexSize);
  std::vector<gpu::Vertex> vertices;
  vertices.reserve(vertexCount);
  std::vector<uint32_t> indices;
  indices.reserve(indexCount);
  std::vector<Mesh::Submesh> submeshes;
  submeshes.reserve(iMeshData.submeshes.size());
  for (const SubmeshData& submesh : iMeshData.submeshes) {
    NE_ASSERT(!submesh.positions.empty() && !submesh.indices.empty(), "Meshes are validated before they are staged");

    // Indices stay relative to their submesh, whose vertices start at its vertex address
    submeshes.push_back(Mesh::Submesh{
        .vertexAddress = mVertexBuffer->getDeviceAddress(vertexOffset + vertices.size() * sizeof(gpu::Vertex)),
        .firstIndex = static_cast<uint32_t>(indexOffset / sizeof(uint32_t) + indices.size()),
        .indexCount = static_cast<uint32_t>(submesh.indices.size()),
    });

    const size_t submeshVertexCount = submesh.positions.size();
    const bool hasNormals = submesh.normals.size() == submeshVertexCount;
    const bool hasTexCoords = submesh.texCoords.size() == submeshVertexCount;
    const bool hasColors = submesh.colors.size() == submeshVertexCount;
    const bool hasTangents = submesh.tangents.size() == submeshVertexCount;
    for (size_t i = 0; i < submeshVertexCount; ++i) {
      vertices.push_back(gpu::Vertex{
          .pos = submesh.positions[i],
          .normal = hasNormals ? submesh.normals[i] : Vec3(0.0f, 0.0f, 1.0f),
          .uv = hasTexCoords ? submesh.texCoords[i] : Vec2(0.0f, 0.0f),
          .color = hasColors ? submesh.colors[i] : LinearColor::White,
          .tangent = hasTangents ? submesh.tangents[i] : Vec4(1.0f, 0.0f, 0.0f, 1.0f),
      });
    }
    indices.insert(indices.end(), submesh.indices.begin(), submesh.indices.end());
  }

  NE_VERBOSE("Allocated geometry: vertex size: {}, index size: {} | "
             "Pool occupancy: vertex={}/{} ({:.2f}%), index={}/{} ({:.2f}%)",
             vk_utils::formatBytes(vertexSize), vk_utils::formatBytes(indexSize),
             vk_utils::formatBytes(mVertexBuffer->getUploadOffset()), vk_utils::formatBytes(mVertexBuffer->getConfig().size),
             (static_cast<double>(mVertexBuffer->getUploadOffset()) / mVertexBuffer->getConfig().size) * 100.0,
             vk_utils::formatBytes(mIndexBuffer->getUploadOffset()), vk_utils::formatBytes(mIndexBuffer->getConfig().size),
             (static_cast<double>(mIndexBuffer->getUploadOffset()) / mIndexBuffer->getConfig().size) * 100.0);

  iStagingManager.stageBufferCopy(mVertexBuffer->getBuffer(), vertices.data(), vertexSize, vertexOffset);
  iStagingManager.stageBufferCopy(mIndexBuffer->getBuffer(), indices.data(), indexSize, indexOffset);
  return Mesh(std::move(submeshes));
}

void GeometryAllocator::bindIndexBuffer(VkCommandBuffer iCommandBuffer) const {
  NE_ASSERT(iCommandBuffer != VK_NULL_HANDLE, "Command buffer must not be null");
  NE_ASSERT(mIndexBuffer, "Index buffer must not be null");
  vkCmdBindIndexBuffer(iCommandBuffer, mIndexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
}

} // namespace ne
