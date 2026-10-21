#pragma once

#include "core/math/math.h"
#include "renderer/mesh.h"
#include <memory>
#include <optional>
#include <vector>
#include <volk/volk.h>

namespace ne {

class Buffer;
class GeometryAllocator;
class Pipeline;
class Registry;

class SceneRenderer {
public:
  struct Config {
    GeometryAllocator* geometryAllocator = nullptr;
  };

  SceneRenderer(const Config& iConfig);
  ~SceneRenderer();

  SceneRenderer(const SceneRenderer&) = delete;
  SceneRenderer& operator=(const SceneRenderer&) = delete;
  SceneRenderer(SceneRenderer&&) = delete;
  SceneRenderer& operator=(SceneRenderer&&) = delete;

  // Finds the primary camera, then gathers and sorts the frame's draws. Returns the upload bytes record() needs.
  VkDeviceSize prepare(Registry* iRegistry, float iAspectRatio);
  // Draws what prepare() gathered into the current rendering scope, writing the per-draw data to ioUploadBuffer. The
  // caller opens the scope, sets the viewport, and binds the bindless set.
  void record(VkCommandBuffer iCommandBuffer, Buffer& ioUploadBuffer);

private:
  struct DrawCall {
    Pipeline* pipeline;
    const Mesh::Submesh* submesh;
    Mat4 transform;
    LinearColor color;
    uint32_t textureIndex;
    uint32_t samplerIndex;
  };

  GeometryAllocator* mGeometryAllocator = nullptr;
  std::optional<Mat4> mViewProj; // Of the primary camera; without one nothing is drawn
  std::vector<DrawCall> mDrawCalls;
};

} // namespace ne
