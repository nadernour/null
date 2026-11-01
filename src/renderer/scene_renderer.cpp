#include "renderer/scene_renderer.h"
#include "components/camera_component.h"
#include "components/environment_component.h"
#include "components/light_component.h"
#include "components/mesh_component.h"
#include "components/transform_component.h"
#include "core/assert.h"
#include "core/ecs.h"
#include "core/material_data.h"
#include "core/math/math_utils.h"
#include "renderer/device.h"
#include "renderer/geometry_allocator.h"
#include "renderer/gpu_types.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/pipeline.h"
#include "renderer/resource_manager.h"
#include "renderer/upload_allocator.h"
#include "renderer/utils.h"

// std
#include <algorithm>
#include <format>
#include <optional>

namespace ne {

namespace {

// What the shader needs of a light. gpu::Light's defaults leave out the cone, which only spot lights have.
gpu::Light toGpuLight(const LightComponent& iLight, const Mat4& iWorld) {
  gpu::Light light{
      .position = Vec4(iWorld[3].x, iWorld[3].y, iWorld[3].z, 1.0f),
      .intensity = iLight.color.toVec3() * iLight.intensity,
      .invRangeSq = iLight.range > 0.0f ? 1.0f / (iLight.range * iLight.range) : 0.0f,
      .direction = Vec3(iWorld[0].x, iWorld[0].y, iWorld[0].z).getSafeNormal(),
  };
  switch (iLight.type) {
    case LightComponent::Type::Directional:
      // A point infinitely far away, the other way from where the light travels; it has no range
      light.position = Vec4(-light.direction, 0.0f);
      light.invRangeSq = 0.0f;
      break;
    case LightComponent::Type::Point:
      break;
    case LightComponent::Type::Spot: {
      // KHR_lights_punctual's cone: full intensity inside the inner angle, none outside the outer one
      const float cosOuter = math::cos(iLight.outerConeAngle);
      light.spotScale = 1.0f / math::max(math::cos(iLight.innerConeAngle) - cosOuter, 0.001f);
      light.spotOffset = -cosOuter * light.spotScale;
      break;
    }
  }
  return light;
}

} // namespace

SceneRenderer::SceneRenderer(Device* iDevice, ResourceManager* iResourceManager, VkFormat iColorFormat, VkFormat iDepthFormat) {
  NE_ASSERT(iDevice && iResourceManager);
  mGeometryAllocator = iResourceManager->getGeometryAllocator();
  mMaterialBufferAddress = iResourceManager->getMaterialBufferAddress();

  // Only the masked entry point discards, so the others keep early depth testing. Blended surfaces test against the
  // opaque depth but don't write it, so what's behind them still draws.
  for (size_t i = 0; i < mPipelines.size(); ++i) {
    const AlphaMode alphaMode = static_cast<AlphaMode>(i);
    const bool isBlended = alphaMode == AlphaMode::Blend;
    const Pipeline::Config config{
        .shaderName = vk_utils::kSceneShader,
        .fragmentEntry = alphaMode == AlphaMode::Mask ? "fragMainMasked" : "fragMain",
        .debugName = std::format("Scene_{}", toString(alphaMode)),
        .layout = iResourceManager->getPipelineLayout(),
        .colorAttachmentFormat = iColorFormat,
        .depthAttachmentFormat = iDepthFormat,
        .depthMode = isBlended ? Pipeline::DepthMode::ReadOnly : Pipeline::DepthMode::ReadWrite,
        .blendMode = isBlended ? Pipeline::BlendMode::Alpha : Pipeline::BlendMode::Disabled,
    };
    mPipelines[i] = std::make_unique<Pipeline>(iDevice->getDevice(), config);
  }
}

SceneRenderer::~SceneRenderer() = default;

void SceneRenderer::prepare(Registry& ioRegistry, float iAspectRatio) {
  mGlobalUniforms.reset();
  mLights.clear();
  mDrawCalls.clear();

  Vec3 cameraPosition;
  ioRegistry.view<TransformComponent, CameraComponent>().each(
      [&](const TransformComponent& iTransform, const CameraComponent& iCamera) {
        if (iCamera.primary) {
          NE_ASSERT(!mGlobalUniforms, "At most one camera may be primary");
          const Mat4& world = iTransform.getWorldMatrix();
          cameraPosition = Vec3(world[3].x, world[3].y, world[3].z);
          // An orthographic camera's rays are parallel, as if its eye were infinitely far behind it: the point at w = 0
          // in its backward direction. Every pixel then gets the same view direction.
          const Vec3 forward = Vec3(world[0].x, world[0].y, world[0].z).getSafeNormal();
          const bool isOrthographic = iCamera.projectionType == CameraComponent::ProjectionType::Orthographic;
          mGlobalUniforms = gpu::GlobalUniforms{
              .viewProj = iCamera.getViewProjectionMatrix(world, iAspectRatio),
              .eye = isOrthographic ? Vec4(-forward, 0.0f) : Vec4(cameraPosition, 1.0f),
              .exposure = iCamera.getExposure(),
          };
        }
      });
  if (!mGlobalUniforms) {
    return;
  }

  // Without an environment, the sky and the ground stay black
  bool hasEnvironment = false;
  ioRegistry.view<EnvironmentComponent>().each([&](const EnvironmentComponent& iEnvironment) {
    NE_ASSERT(!hasEnvironment, "A scene may have at most one environment");
    hasEnvironment = true;
    mGlobalUniforms->skyRadiance = iEnvironment.skyColor.toVec3() * iEnvironment.skyIntensity;
    mGlobalUniforms->groundRadiance = iEnvironment.groundColor.toVec3() * iEnvironment.groundIntensity;
  });

  ioRegistry.view<TransformComponent, LightComponent>().each(
      [&](const TransformComponent& iTransform, const LightComponent& iLight) {
        mLights.push_back(toGpuLight(iLight, iTransform.getWorldMatrix()));
      });
  mGlobalUniforms->lightCount = static_cast<uint32_t>(mLights.size());

  ioRegistry.view<TransformComponent, MeshComponent>().each(
      [&](Entity, const TransformComponent& iTransform, const MeshComponent& iMesh) {
        if (!iMesh.mesh) {
          return;
        }
        const Mat4& world = iTransform.getWorldMatrix();
        const float distanceSq = (Vec3(world[3].x, world[3].y, world[3].z) - cameraPosition).lengthSquared();
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
                                          .distanceSq = distanceSq});
          }
        }
      });

  // Opaque draws first, so the depth they write hides what's behind them from the masked draws, whose discard keeps
  // them from writing depth early. Blended draws come last, from far to near, so each blends over what's behind it.
  // Within an alpha mode, single-sided draws come before double-sided ones, so the cull mode changes once.
  // Adjacent draws of the same submesh become one instanced indirect draw; its instances draw in order, so blended ones
  // stay far to near.
  std::sort(mDrawCalls.begin(), mDrawCalls.end(), [](const DrawCall& iA, const DrawCall& iB) {
    if (iA.alphaMode != iB.alphaMode) {
      return iA.alphaMode < iB.alphaMode;
    }
    if (iA.alphaMode == AlphaMode::Blend && iA.distanceSq != iB.distanceSq) {
      return iA.distanceSq > iB.distanceSq;
    }
    if (iA.doubleSided != iB.doubleSided) {
      return iA.doubleSided < iB.doubleSided;
    }
    return iA.submesh < iB.submesh;
  });
}

void SceneRenderer::record(VkCommandBuffer iCommandBuffer, UploadAllocator& ioUploadAllocator, gpu::DebugView iDebugView) const {
  if (!mGlobalUniforms || mDrawCalls.empty()) {
    return;
  }

  NE_ASSERT(iCommandBuffer != VK_NULL_HANDLE);

  mGeometryAllocator->bindIndexBuffer(iCommandBuffer);

  // Without lights the address stays 0, and the shader's light loop never reads it: an empty vector's data() may be
  // null, which memcpy mustn't get, even for 0 bytes
  gpu::GlobalUniforms globalUniforms = *mGlobalUniforms;
  if (!mLights.empty()) {
    globalUniforms.lights = ioUploadAllocator.upload(mLights.data(), mLights.size() * sizeof(gpu::Light)).address;
  }
  globalUniforms.debugView = static_cast<uint32_t>(iDebugView);
  VkDeviceAddress globalUniformsAddr = ioUploadAllocator.upload(&globalUniforms, sizeof(gpu::GlobalUniforms)).address;

  // Each multi-draw holds the draws of one alpha mode and one sidedness. The alpha mode picks the pipeline, and the
  // sidedness the cull mode, which every scene pipeline takes as dynamic state.
  size_t i = 0;
  const Pipeline* boundPipeline = nullptr;
  std::optional<VkCullModeFlags> boundCullMode; // A new command buffer has none until the first batch sets it
  while (i < mDrawCalls.size()) {
    const AlphaMode alphaMode = mDrawCalls[i].alphaMode;
    const bool doubleSided = mDrawCalls[i].doubleSided;
    Pipeline& pipeline = *mPipelines[static_cast<size_t>(alphaMode)];
    if (boundPipeline != &pipeline) {
      boundPipeline = &pipeline;
      pipeline.bind(iCommandBuffer);
    }
    const VkCullModeFlags cullMode = doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    if (boundCullMode != cullMode) {
      boundCullMode = cullMode;
      vkCmdSetCullMode(iCommandBuffer, cullMode);
    }
    const auto isInBatch = [&](const DrawCall& iDrawCall) {
      return iDrawCall.alphaMode == alphaMode && iDrawCall.doubleSided == doubleSided;
    };

    std::vector<gpu::DrawInfo> drawInfos;
    std::vector<gpu::InstanceData> instanceData;
    std::vector<VkDrawIndexedIndirectCommand> indirectCommands;

    while (i < mDrawCalls.size() && isInBatch(mDrawCalls[i])) {
      const Mesh::Submesh* currentSubmesh = mDrawCalls[i].submesh;
      uint32_t startInstanceOffset = static_cast<uint32_t>(instanceData.size());

      uint32_t instanceCount = 0;
      while (i < mDrawCalls.size() && isInBatch(mDrawCalls[i]) && mDrawCalls[i].submesh == currentSubmesh) {
        Mat4 normalMatrix = mDrawCalls[i].transform.inversed().transposed();
        instanceData.push_back(gpu::InstanceData{.modelMatrix = mDrawCalls[i].transform,
                                                 .normalMatrix = normalMatrix,
                                                 .color = mDrawCalls[i].color,
                                                 .materialIndex = mDrawCalls[i].materialIndex});
        instanceCount++;
        i++;
      }

      gpu::DrawInfo drawInfo{};
      drawInfo.vertices = currentSubmesh->vertexAddress;
      drawInfo.instanceBaseOffset = startInstanceOffset;
      drawInfos.push_back(drawInfo);

      VkDrawIndexedIndirectCommand indirectCmd{};
      indirectCmd.indexCount = currentSubmesh->indexCount;
      indirectCmd.instanceCount = instanceCount;
      indirectCmd.firstIndex = currentSubmesh->firstIndex;
      indirectCmd.vertexOffset = 0;
      indirectCmd.firstInstance = 0;
      indirectCommands.push_back(indirectCmd);
    }

    uint32_t numUniqueSubmeshes = static_cast<uint32_t>(drawInfos.size());

    VkDeviceAddress drawInfosAddr = ioUploadAllocator.upload(drawInfos.data(), drawInfos.size() * sizeof(gpu::DrawInfo)).address;
    VkDeviceAddress instancesAddr =
        ioUploadAllocator.upload(instanceData.data(), instanceData.size() * sizeof(gpu::InstanceData)).address;
    UploadAllocator::Allocation indirectAllocation =
        ioUploadAllocator.upload(indirectCommands.data(), indirectCommands.size() * sizeof(VkDrawIndexedIndirectCommand));

    gpu::ScenePushConstants pc{};
    pc.drawInfos = drawInfosAddr;
    pc.globalUniforms = globalUniformsAddr;
    pc.instances = instancesAddr;
    pc.materials = mMaterialBufferAddress;

    vkCmdPushConstants(iCommandBuffer, pipeline.getPipelineLayout(), gpu::kPushConstantStages, 0, sizeof(gpu::ScenePushConstants),
                       &pc);

    vkCmdDrawIndexedIndirect(iCommandBuffer, indirectAllocation.buffer, indirectAllocation.offset, numUniqueSubmeshes,
                             sizeof(VkDrawIndexedIndirectCommand));
  }
}

} // namespace ne
