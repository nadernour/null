#pragma once

#include "core/math/math.h"
#include "renderer/mesh.h"
#include <memory>
#include <vector>
#include <volk/volk.h>

namespace ne {

class Device;
class FrameRenderer;
class GeometryAllocator;
class BindlessManager;
class ImGuiManager;
class Pipeline;
class Registry;

class SceneRenderer {
public:
  struct Config {
    Device* device = nullptr;
    GeometryAllocator* geometryAllocator = nullptr;
    BindlessManager* bindlessManager = nullptr;
    VkPipelineLayout scenePipelineLayout = VK_NULL_HANDLE;
    FrameRenderer* frameRenderer = nullptr;
  };

  SceneRenderer(const Config& iConfig);
  ~SceneRenderer();

  SceneRenderer(const SceneRenderer&) = delete;
  SceneRenderer& operator=(const SceneRenderer&) = delete;
  SceneRenderer(SceneRenderer&&) = delete;
  SceneRenderer& operator=(SceneRenderer&&) = delete;

  void waitIdle();

  void render(VkCommandBuffer iCommandBuffer, Registry* iRegistry, ImGuiManager* iGuiManager = nullptr);
  void draw(Registry* iRegistry, ImGuiManager* iGuiManager = nullptr);

private:
  void submit(VkCommandBuffer iCommandBuffer, const Mat4& iViewProj);

  struct DrawCall {
    Pipeline* pipeline;
    const Mesh::Submesh* submesh;
    Mat4 transform;
    LinearColor color;
    uint32_t textureIndex;
    uint32_t samplerIndex;
  };

  Device* mDevice = nullptr;
  GeometryAllocator* mGeometryAllocator = nullptr;
  BindlessManager* mBindlessManager = nullptr;
  VkPipelineLayout mScenePipelineLayout = VK_NULL_HANDLE;
  FrameRenderer* mFrameRenderer = nullptr;
  std::vector<DrawCall> mDrawCalls;
};

} // namespace ne
