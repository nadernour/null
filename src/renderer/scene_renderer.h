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

// One rendering of the scene, into the pass the caller opened: from the camera into the HDR target, or from a light into
// a shadow map tile
struct SceneView {
  enum class Kind : uint8_t {
    Color, // Lit, through the scene pipelines
    Depth  // Depth only, through the shadow pipelines; blended draws cast no shadows
  };

  Kind kind = Kind::Color;
  Mat4 viewProj;
  Vec4 eye{0.0f, 0.0f, 0.0f, 1.0f}; // As in gpu::GlobalUniforms
  float exposure = 1.0f;
  gpu::DebugView debugView{};  // Lit
  VkDeviceAddress shadows = 0; // The gpu::ShadowData a color view shades with; 0 for none
};

// Draws the scene from any number of views a frame: prepare() gathers it and upload() sends it to the GPU once, then
// recordView() draws it from each view
class SceneRenderer {
public:
  // The first directional light that casts shadows
  struct ShadowLight {
    uint32_t lightIndex; // In the frame's light buffer
    Mat4 world;          // Its entity's world matrix; it shines along +X
  };

  // Draws iResourceManager's meshes and materials, with a pipeline per alpha mode built for targets of iColorFormat and
  // iDepthFormat, and shadow pipelines for shadow maps of iDepthFormat
  SceneRenderer(Device* iDevice, ResourceManager* iResourceManager, VkFormat iColorFormat, VkFormat iDepthFormat);
  ~SceneRenderer();

  SceneRenderer(const SceneRenderer&) = delete;
  SceneRenderer& operator=(const SceneRenderer&) = delete;
  SceneRenderer(SceneRenderer&&) = delete;
  SceneRenderer& operator=(SceneRenderer&&) = delete;

  // Gathers the frame's lights, environment, and draws, and sorts the draws: opaque, then masked, then blended from far
  // to near as iCameraView sees them. CPU only, so it runs before the frame waits for its fence.
  void prepare(Registry& ioRegistry, const SceneView& iCameraView);
  // What prepare() found; empty when no directional light casts shadows
  const std::optional<ShadowLight>& getShadowLight() const { return mShadowLight; }
  // Uploads what every view draws, once per frame after prepare(): the lights and each draw's data, grouped into batches
  void upload(UploadAllocator& ioUploadAllocator);
  // Draws the batches from iView into the current pass. The caller opens the pass, sets the viewport, and binds the
  // bindless set. A depth view draws with the shadow pipelines and skips blended draws.
  void recordView(VkCommandBuffer iCommandBuffer, UploadAllocator& ioUploadAllocator, const SceneView& iView) const;

private:
  struct DrawCall {
    const Mesh::Submesh* submesh;
    Mat4 transform;
    LinearColor color;
    uint32_t materialIndex;
    AlphaMode alphaMode; // Picks the pipeline
    bool doubleSided;    // Picks the cull mode
    float distance;      // From the camera to the entity's origin; orders blended draws
  };

  // Draws of one alpha mode and one sidedness: one multi-draw, with one pipeline and one cull mode
  struct Batch {
    AlphaMode alphaMode;
    bool doubleSided;
    uint32_t firstDraw; // Into the frame's draw infos and indirect commands
    uint32_t drawCount;
  };

  GeometryAllocator* mGeometryAllocator = nullptr;
  VkDeviceAddress mMaterialBufferAddress = 0; // The material buffer never moves, so its address holds
  // Indexed by AlphaMode. All are built up front, so no pipeline compiles mid-run when a new kind of material appears.
  std::array<std::unique_ptr<Pipeline>, static_cast<size_t>(AlphaMode::Count)> mPipelines;
  // The same for depth views; blended surfaces cast no shadows, so their slot stays empty
  std::array<std::unique_ptr<Pipeline>, static_cast<size_t>(AlphaMode::Count)> mShadowPipelines;

  // What prepare() gathers
  gpu::GlobalUniforms mFrameUniforms; // The environment and the light count; upload() adds the lights' address
  std::vector<gpu::Light> mLights;
  std::optional<ShadowLight> mShadowLight;
  std::vector<DrawCall> mDrawCalls;

  // What upload() leaves for recordView(): every batch's data, one after another
  VkDeviceAddress mDrawInfos = 0; // gpu::DrawInfo, one per indirect command
  VkDeviceAddress mInstances = 0; // gpu::InstanceData, one per draw call
  UploadAllocator::Allocation mIndirectCommands;
  std::vector<Batch> mBatches;
};

} // namespace ne
