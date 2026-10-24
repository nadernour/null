#pragma once

#include "core/math/math.h"
#include "renderer/mesh.h"
#include <memory>
#include <optional>
#include <vector>
#include <volk/volk.h>

namespace ne {

class GeometryAllocator;
class Pipeline;
class Registry;
class UploadAllocator;

namespace gpu {
enum class DebugView : uint32_t;
}

class SceneRenderer {
public:
  // iMaterialBufferAddress is ResourceManager's material buffer, which never moves
  SceneRenderer(GeometryAllocator* iGeometryAllocator, VkDeviceAddress iMaterialBufferAddress);
  ~SceneRenderer();

  SceneRenderer(const SceneRenderer&) = delete;
  SceneRenderer& operator=(const SceneRenderer&) = delete;
  SceneRenderer(SceneRenderer&&) = delete;
  SceneRenderer& operator=(SceneRenderer&&) = delete;

  // Finds the primary camera, then gathers and sorts the frame's draws
  void prepare(Registry& ioRegistry, float iAspectRatio);
  // Draws what prepare() gathered into the current pass, writing the per-draw data through ioUploadAllocator. The caller
  // opens the pass, sets the viewport, and binds the bindless set.
  void record(VkCommandBuffer iCommandBuffer, UploadAllocator& ioUploadAllocator, gpu::DebugView iDebugView);

private:
  struct DrawCall {
    Pipeline* pipeline;
    const Mesh::Submesh* submesh;
    Mat4 transform;
    LinearColor color;
    uint32_t materialIndex;
  };

  GeometryAllocator* mGeometryAllocator = nullptr;
  VkDeviceAddress mMaterialBufferAddress = 0;
  std::optional<Mat4> mViewProj; // Of the primary camera; without one nothing is drawn
  std::vector<DrawCall> mDrawCalls;
};

} // namespace ne
