#pragma once

#include "core/material_data.h"
#include "core/math/math.h"
#include "renderer/gpu_types.h"
#include "renderer/mesh.h"
#include "renderer/upload_allocator.h"
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

// One rendering of the scene: from the camera into the HDR target, or from a light into a shadow map tile
struct SceneView {
  enum class Kind : uint8_t { Color, Depth };

  Kind kind = Kind::Color;
  Mat4 viewProj;
  Vec4 eye{0.0f, 0.0f, 0.0f, 1.0f}; // As in gpu::GlobalUniforms
  float exposure = 1.0f;
  gpu::DebugView debugView{};  // Lit
  VkDeviceAddress shadows = 0; // gpu::ShadowData; 0 for none
};

// Draws the scene from any number of views a frame: prepare() and upload() once, then recordView() for each view
class SceneRenderer {
public:
  // The first directional light that casts shadows
  struct ShadowLight {
    uint32_t lightIndex; // In the frame's light buffer
    Mat4 world;
  };

  SceneRenderer(Device* iDevice, ResourceManager* iResourceManager, VkFormat iColorFormat, VkFormat iDepthFormat);
  ~SceneRenderer();

  SceneRenderer(const SceneRenderer&) = delete;
  SceneRenderer& operator=(const SceneRenderer&) = delete;
  SceneRenderer(SceneRenderer&&) = delete;
  SceneRenderer& operator=(SceneRenderer&&) = delete;

  // Gathers the frame's lights, environment, and draws; iCameraView orders the blended ones. CPU only, so it can run
  // before the frame waits for its fence.
  void prepare(Registry& ioRegistry, const SceneView& iCameraView);
  // Empty when no directional light casts shadows
  const std::optional<ShadowLight>& getShadowLight() const { return mShadowLight; }
  void upload(UploadAllocator& ioUploadAllocator);
  // Draws into the current pass; the caller opens it, sets the viewport, and binds the bindless set
  void recordView(VkCommandBuffer iCommandBuffer, UploadAllocator& ioUploadAllocator, const SceneView& iView) const;

private:
  struct DrawCall {
    const Mesh::Submesh* submesh;
    Mat4 transform;
    LinearColor color;
    uint32_t materialIndex;
    AlphaMode alphaMode;
    bool doubleSided;
    float distance; // From the camera to the entity's origin; orders blended draws
  };

  // One multi-draw: one alpha mode (the pipeline) and one sidedness (the cull mode)
  struct Batch {
    AlphaMode alphaMode;
    bool doubleSided;
    uint32_t firstDraw; // Into the frame's draw infos and indirect commands
    uint32_t drawCount;
  };

  GeometryAllocator* mGeometryAllocator = nullptr;
  VkDeviceAddress mMaterialBufferAddress = 0;
  // Indexed by AlphaMode, all built up front
  std::array<std::unique_ptr<Pipeline>, static_cast<size_t>(AlphaMode::Count)> mPipelines;
  // The same for depth views; the Blend slot stays empty
  std::array<std::unique_ptr<Pipeline>, static_cast<size_t>(AlphaMode::Count)> mShadowPipelines;

  // What prepare() gathers
  gpu::GlobalUniforms mFrameUniforms; // The environment and the light count; upload() adds the lights' address
  std::vector<gpu::Light> mLights;
  std::optional<ShadowLight> mShadowLight;
  std::vector<DrawCall> mDrawCalls;

  // What upload() leaves for recordView()
  VkDeviceAddress mDrawInfos = 0; // gpu::DrawInfo, one per indirect command
  VkDeviceAddress mInstances = 0; // gpu::InstanceData, one per draw call
  UploadAllocator::Allocation mIndirectCommands;
  std::vector<Batch> mBatches;
};

} // namespace ne
