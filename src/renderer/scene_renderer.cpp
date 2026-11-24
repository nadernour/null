#include "renderer/scene_renderer.h"
#include "components/environment_component.h"
#include "components/light_component.h"
#include "components/mesh_component.h"
#include "components/transform_component.h"
#include "core/assert.h"
#include "core/ecs.h"
#include "core/material_data.h"
#include "core/math/math_utils.h"
#include "renderer/geometry_allocator.h"
#include "renderer/gpu_types.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/resource_manager.h"
#include "renderer/rhi/device.h"
#include "renderer/rhi/pipeline.h"
#include "renderer/rhi/upload_allocator.h"
#include "renderer/rhi/utils.h"
#include "renderer/shadow_cascades.h"

// std
#include <algorithm>
#include <format>
#include <optional>

namespace ne {

namespace {

gpu::Light toGpuLight(const LightComponent& iLight, const Mat4& iWorld) {
  gpu::Light light{
      .position = Vec4(iWorld[3].xyz(), 1.0f),
      .intensity = iLight.color.toVec3() * iLight.intensity,
      .invRangeSq = iLight.range > 0.0f ? 1.0f / (iLight.range * iLight.range) : 0.0f,
      .direction = iWorld[0].xyz().getSafeNormal(),
  };
  switch (iLight.type) {
    case LightComponent::Type::Directional:
      light.position = Vec4(-light.direction, 0.0f);
      light.invRangeSq = 0.0f;
      break;
    case LightComponent::Type::Point:
      break;
    case LightComponent::Type::Spot: {
      // KHR_lights_punctual's cone falloff
      const float cosOuter = math::cos(iLight.outerConeAngle);
      light.spotScale = 1.0f / math::max(math::cos(iLight.innerConeAngle) - cosOuter, 0.001f);
      light.spotOffset = -cosOuter * light.spotScale;
      break;
    }
  }
  return light;
}

// Blended surfaces cast no shadows: a shadow map holds one depth per texel, with no room for partial coverage
constexpr bool castsShadows(AlphaMode iAlphaMode) { return iAlphaMode != AlphaMode::Blend; }

} // namespace

SceneRenderer::SceneRenderer(Device* iDevice, ResourceManager* iResourceManager, VkFormat iColorFormat, VkFormat iDepthFormat) {
  NE_ASSERT(iDevice && iResourceManager);
  mGeometryAllocator = iResourceManager->getGeometryAllocator();
  mMaterialBufferAddress = iResourceManager->getMaterialBufferAddress();

  // Only Mask discards, so the others keep early depth testing; Blend tests depth without writing it
  for (size_t i = 0; i < mPipelines.size(); ++i) {
    const AlphaMode alphaMode = static_cast<AlphaMode>(i);
    const bool isBlended = alphaMode == AlphaMode::Blend;
    const Pipeline::Config config{
        .shader = {.name = vk_utils::kSceneShader, .fragmentEntry = alphaMode == AlphaMode::Mask ? "fragMainMasked" : "fragMain"},
        .debugName = std::format("Scene_{}", toString(alphaMode)),
        .layout = iResourceManager->getPipelineLayout(),
        .colorAttachmentFormat = iColorFormat,
        .depth = {.format = iDepthFormat, .mode = isBlended ? Pipeline::DepthMode::ReadOnly : Pipeline::DepthMode::ReadWrite},
        .blendMode = isBlended ? Pipeline::BlendMode::Alpha : Pipeline::BlendMode::Disabled,
    };
    mPipelines[i] = std::make_unique<Pipeline>(iDevice->getDevice(), config);
  }

  // Opaque casters need no fragment shader. Depth clamp keeps casters in front of a cascade's near plane casting.
  for (size_t i = 0; i < mShadowPipelines.size(); ++i) {
    const AlphaMode alphaMode = static_cast<AlphaMode>(i);
    if (!castsShadows(alphaMode)) {
      continue;
    }
    const Pipeline::Config config{
        .shader = {.name = vk_utils::kSceneShader, .fragmentEntry = alphaMode == AlphaMode::Mask ? "fragShadowMasked" : ""},
        .debugName = std::format("Shadow_{}", toString(alphaMode)),
        .layout = iResourceManager->getPipelineLayout(),
        .depth = {.format = iDepthFormat, .biasSlope = ShadowCascades::kDepthBiasSlope, .clamp = true},
    };
    mShadowPipelines[i] = std::make_unique<Pipeline>(iDevice->getDevice(), config);
  }
}

SceneRenderer::~SceneRenderer() = default;

void SceneRenderer::prepare(Registry& ioRegistry, const SceneView& iCameraView) {
  mFrameUniforms = gpu::GlobalUniforms{};
  mLights.clear();
  mShadowLight.reset();
  mDrawCalls.clear();

  // Without an environment, the sky and the ground stay black
  bool hasEnvironment = false;
  ioRegistry.view<EnvironmentComponent>().each([&](const EnvironmentComponent& iEnvironment) {
    NE_ASSERT(!hasEnvironment, "A scene may have at most one environment");
    hasEnvironment = true;
    mFrameUniforms.skyRadiance = iEnvironment.skyColor.toVec3() * iEnvironment.skyIntensity;
    mFrameUniforms.groundRadiance = iEnvironment.groundColor.toVec3() * iEnvironment.groundIntensity;
  });

  ioRegistry.view<TransformComponent, LightComponent>().each(
      [&](const TransformComponent& iTransform, const LightComponent& iLight) {
        const Mat4& world = iTransform.getWorldMatrix();
        if (!mShadowLight && iLight.castShadows && iLight.type == LightComponent::Type::Directional) {
          mShadowLight = ShadowLight{.lightIndex = static_cast<uint32_t>(mLights.size()), .world = world};
        }
        mLights.push_back(toGpuLight(iLight, world));
      });
  mFrameUniforms.lightCount = static_cast<uint32_t>(mLights.size());

  // An orthographic eye is a direction (w = 0), so there the distance is depth along the view from the world origin,
  // which differs from depth from the camera by the same amount for every draw
  const Vec3 eye = iCameraView.eye.xyz();
  const bool isOrthographic = iCameraView.eye.w == 0.0f;
  ioRegistry.view<TransformComponent, MeshComponent>().each(
      [&](Entity, const TransformComponent& iTransform, const MeshComponent& iMesh) {
        if (!iMesh.mesh) {
          return;
        }
        const Mat4& world = iTransform.getWorldMatrix();
        const Vec3 position = world[3].xyz();
        const float distance = isOrthographic ? position.dot(-eye) : (position - eye).length();
        const std::vector<Mesh::Submesh>& submeshes = iMesh.mesh->getSubmeshes();
        const size_t drawnCount = math::min(submeshes.size(), iMesh.materials.size());
        for (size_t i = 0; i < drawnCount; ++i) {
          const Material* material = iMesh.materials[i].get();
          if (material) {
            mDrawCalls.push_back(DrawCall{.submesh = &submeshes[i],
                                          .transform = world,
                                          .color = iMesh.colorTint,
                                          .materialIndex = material->getMaterialIndex(),
                                          .alphaMode = material->getAlphaMode(),
                                          .doubleSided = material->isDoubleSided(),
                                          .distance = distance});
          }
        }
      });

  // Opaque first, to fill depth before the masked draws, whose discard keeps them from writing depth early; blended last,
  // far to near, so they blend in order. Sidedness then groups the cull modes, and submesh last puts the draws upload()
  // merges together.
  std::sort(mDrawCalls.begin(), mDrawCalls.end(), [](const DrawCall& iA, const DrawCall& iB) {
    if (iA.alphaMode != iB.alphaMode) {
      return iA.alphaMode < iB.alphaMode;
    }
    if (iA.alphaMode == AlphaMode::Blend && iA.distance != iB.distance) {
      return iA.distance > iB.distance;
    }
    if (iA.doubleSided != iB.doubleSided) {
      return iA.doubleSided < iB.doubleSided;
    }
    return iA.submesh < iB.submesh;
  });
}

void SceneRenderer::upload(UploadAllocator& ioUploadAllocator) {
  mBatches.clear();

  // Without lights the address is 0, and the shader's light loop never reads it: an empty vector's data() may be null,
  // which memcpy mustn't get, even for 0 bytes
  mFrameUniforms.lights =
      mLights.empty() ? 0 : ioUploadAllocator.upload(mLights.data(), mLights.size() * sizeof(gpu::Light)).address;
  if (mDrawCalls.empty()) {
    return;
  }

  // Adjacent draws of one submesh become one instanced draw; instances draw in order, so blended ones stay far to near
  std::vector<gpu::DrawInfo> drawInfos;
  std::vector<gpu::InstanceData> instanceData;
  std::vector<VkDrawIndexedIndirectCommand> indirectCommands;
  size_t i = 0;
  while (i < mDrawCalls.size()) {
    const AlphaMode alphaMode = mDrawCalls[i].alphaMode;
    const bool doubleSided = mDrawCalls[i].doubleSided;
    const auto isInBatch = [&](const DrawCall& iDrawCall) {
      return iDrawCall.alphaMode == alphaMode && iDrawCall.doubleSided == doubleSided;
    };
    const uint32_t firstDraw = static_cast<uint32_t>(drawInfos.size());

    while (i < mDrawCalls.size() && isInBatch(mDrawCalls[i])) {
      const Mesh::Submesh* submesh = mDrawCalls[i].submesh;
      const uint32_t firstInstance = static_cast<uint32_t>(instanceData.size());
      while (i < mDrawCalls.size() && isInBatch(mDrawCalls[i]) && mDrawCalls[i].submesh == submesh) {
        const DrawCall& drawCall = mDrawCalls[i];
        instanceData.push_back(gpu::InstanceData{.modelMatrix = drawCall.transform,
                                                 .normalMatrix = drawCall.transform.inversed().transposed(),
                                                 .color = drawCall.color,
                                                 .materialIndex = drawCall.materialIndex});
        ++i;
      }

      drawInfos.push_back(gpu::DrawInfo{.vertices = submesh->vertexAddress, .instanceBaseOffset = firstInstance});
      indirectCommands.push_back(VkDrawIndexedIndirectCommand{
          .indexCount = submesh->indexCount,
          .instanceCount = static_cast<uint32_t>(instanceData.size()) - firstInstance,
          .firstIndex = submesh->firstIndex,
          .vertexOffset = 0,
          .firstInstance = 0,
      });
    }

    mBatches.push_back(Batch{.alphaMode = alphaMode,
                             .doubleSided = doubleSided,
                             .firstDraw = firstDraw,
                             .drawCount = static_cast<uint32_t>(drawInfos.size()) - firstDraw});
  }

  mDrawInfos = ioUploadAllocator.upload(drawInfos.data(), drawInfos.size() * sizeof(gpu::DrawInfo)).address;
  mInstances = ioUploadAllocator.upload(instanceData.data(), instanceData.size() * sizeof(gpu::InstanceData)).address;
  mIndirectCommands =
      ioUploadAllocator.upload(indirectCommands.data(), indirectCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
}

void SceneRenderer::recordView(VkCommandBuffer iCommandBuffer, UploadAllocator& ioUploadAllocator, const SceneView& iView) const {
  if (mBatches.empty()) {
    return;
  }

  NE_ASSERT(iCommandBuffer != VK_NULL_HANDLE);

  mGeometryAllocator->bindIndexBuffer(iCommandBuffer);

  gpu::GlobalUniforms globalUniforms = mFrameUniforms;
  globalUniforms.viewProj = iView.viewProj;
  globalUniforms.eye = iView.eye;
  globalUniforms.exposure = iView.exposure;
  globalUniforms.debugView = static_cast<uint32_t>(iView.debugView);
  // A view with shadows shades with the light that casts them
  globalUniforms.shadows = iView.shadows;
  globalUniforms.shadowLight = iView.shadows != 0 && mShadowLight ? mShadowLight->lightIndex : gpu::kNoShadowLight;
  const VkDeviceAddress globalUniformsAddress = ioUploadAllocator.upload(&globalUniforms, sizeof(gpu::GlobalUniforms)).address;

  const bool isShadowView = iView.kind == SceneView::Kind::Shadow;
  const auto& pipelines = isShadowView ? mShadowPipelines : mPipelines;
  const Pipeline* boundPipeline = nullptr;
  std::optional<VkCullModeFlags> boundCullMode; // Unknown until the first batch sets it
  for (const Batch& batch : mBatches) {
    if (isShadowView && !castsShadows(batch.alphaMode)) {
      continue;
    }
    Pipeline& pipeline = *pipelines[static_cast<size_t>(batch.alphaMode)];
    if (boundPipeline != &pipeline) {
      boundPipeline = &pipeline;
      pipeline.bind(iCommandBuffer);
    }
    const VkCullModeFlags cullMode = batch.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    if (boundCullMode != cullMode) {
      boundCullMode = cullMode;
      vkCmdSetCullMode(iCommandBuffer, cullMode);
    }

    // SV_DrawIndex counts from 0 in each multi-draw, so the draw infos start at the batch's first
    const gpu::ScenePushConstants constants{
        .drawInfos = mDrawInfos + batch.firstDraw * sizeof(gpu::DrawInfo),
        .globalUniforms = globalUniformsAddress,
        .instances = mInstances,
        .materials = mMaterialBufferAddress,
    };
    gpu::pushConstants(iCommandBuffer, pipeline.getPipelineLayout(), constants);

    vkCmdDrawIndexedIndirect(iCommandBuffer, mIndirectCommands.buffer,
                             mIndirectCommands.offset + batch.firstDraw * sizeof(VkDrawIndexedIndirectCommand), batch.drawCount,
                             sizeof(VkDrawIndexedIndirectCommand));
  }
}

} // namespace ne
