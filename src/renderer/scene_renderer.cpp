#include "renderer/scene_renderer.h"
#include "components/camera_component.h"
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
  mViewProj.reset();
  mDrawCalls.clear();

  Vec3 cameraPosition;
  ioRegistry.view<TransformComponent, CameraComponent>().each(
      [&](const TransformComponent& iTransform, const CameraComponent& iCamera) {
        if (iCamera.primary) {
          NE_ASSERT(!mViewProj, "At most one camera may be primary");
          const Mat4& world = iTransform.getWorldMatrix();
          mViewProj = iCamera.getViewProjectionMatrix(world, iAspectRatio);
          cameraPosition = Vec3(world[3].x, world[3].y, world[3].z);
        }
      });
  if (!mViewProj) {
    return;
  }

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

void SceneRenderer::record(VkCommandBuffer iCommandBuffer, UploadAllocator& ioUploadAllocator, gpu::DebugView iDebugView) {
  if (!mViewProj || mDrawCalls.empty()) {
    return;
  }

  NE_ASSERT(iCommandBuffer != VK_NULL_HANDLE);

  mGeometryAllocator->bindIndexBuffer(iCommandBuffer);

  gpu::GlobalUniforms globalUniforms;
  globalUniforms.viewProj = *mViewProj;
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
