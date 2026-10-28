#pragma once

#include "core/material_data.h"
#include "core/math/math.h"
#include "renderer/mesh.h"
#include <array>
#include <memory>
#include <optional>
#include <vector>
#include <volk/volk.h>

namespace ne {

class Device;
class GeometryAllocator;
class Pipeline;
class Registry;
class ResourceManager;
class UploadAllocator;

namespace gpu {
enum class DebugView : uint32_t;
}

class SceneRenderer {
public:
  // Draws iResourceManager's meshes and materials, with a pipeline per alpha mode built for targets of iColorFormat and
  // iDepthFormat
  SceneRenderer(Device* iDevice, ResourceManager* iResourceManager, VkFormat iColorFormat, VkFormat iDepthFormat);
  ~SceneRenderer();

  SceneRenderer(const SceneRenderer&) = delete;
  SceneRenderer& operator=(const SceneRenderer&) = delete;
  SceneRenderer(SceneRenderer&&) = delete;
  SceneRenderer& operator=(SceneRenderer&&) = delete;

  // Finds the primary camera, then gathers and sorts the frame's draws: opaque, then masked, then blended from far to
  // near
  void prepare(Registry& ioRegistry, float iAspectRatio);
  // Draws what prepare() gathered into the current pass, writing the per-draw data through ioUploadAllocator. The caller
  // opens the pass, sets the viewport, and binds the bindless set.
  void record(VkCommandBuffer iCommandBuffer, UploadAllocator& ioUploadAllocator, gpu::DebugView iDebugView);

private:
  struct DrawCall {
    const Mesh::Submesh* submesh;
    Mat4 transform;
    LinearColor color;
    uint32_t materialIndex;
    AlphaMode alphaMode; // Picks the pipeline
    bool doubleSided;    // Picks the cull mode
    float distanceSq;    // From the camera to the entity's origin; orders blended draws
  };

  GeometryAllocator* mGeometryAllocator = nullptr;
  VkDeviceAddress mMaterialBufferAddress = 0; // The material buffer never moves, so its address holds
  // Indexed by AlphaMode. All are built up front, so no pipeline compiles mid-run when a new kind of material appears.
  std::array<std::unique_ptr<Pipeline>, static_cast<size_t>(AlphaMode::Count)> mPipelines;
  std::optional<Mat4> mViewProj; // Of the primary camera; without one nothing is drawn
  std::vector<DrawCall> mDrawCalls;
};

} // namespace ne
