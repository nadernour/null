#pragma once

#include "renderer/mesh.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <volk/volk.h>

namespace ne {

class Device;
class Buffer;
class StagingManager;
struct MeshData;

class GeometryAllocator {
public:
  GeometryAllocator(Device* iDevice, VkDeviceSize iVertexPoolSize, VkDeviceSize iIndexPoolSize);
  ~GeometryAllocator();

  GeometryAllocator(const GeometryAllocator&) = delete;
  GeometryAllocator& operator=(const GeometryAllocator&) = delete;
  GeometryAllocator(GeometryAllocator&&) = delete;
  GeometryAllocator& operator=(GeometryAllocator&&) = delete;

  // Sub-allocates one vertex and one index range for the whole mesh and stages their copies into StagingManager. Null,
  // with the shortfall logged, when the mesh doesn't fit in the space left; nothing is allocated then.
  std::optional<Mesh> stageMesh(StagingManager& iStagingManager, const MeshData& iMeshData);

  void bindIndexBuffer(VkCommandBuffer iCommandBuffer) const;

  Buffer* getVertexBuffer() const { return mVertexBuffer.get(); }
  Buffer* getIndexBuffer() const { return mIndexBuffer.get(); }

private:
  std::unique_ptr<Buffer> mVertexBuffer;
  std::unique_ptr<Buffer> mIndexBuffer;
};

} // namespace ne
