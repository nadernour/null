#include "renderer/scene_renderer.h"
#include "components/camera_component.h"
#include "components/mesh_component.h"
#include "components/transform_component.h"
#include "core/assert.h"
#include "core/ecs.h"
#include "core/math/math_utils.h"
#include "renderer/geometry_allocator.h"
#include "renderer/gpu_types.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/pipeline.h"
#include "renderer/upload_allocator.h"

// std
#include <algorithm>
#include <optional>

namespace ne {

SceneRenderer::SceneRenderer(GeometryAllocator* iGeometryAllocator) : mGeometryAllocator(iGeometryAllocator) {
  NE_ASSERT(mGeometryAllocator);
}

SceneRenderer::~SceneRenderer() = default;

void SceneRenderer::prepare(Registry& ioRegistry, float iAspectRatio) {
  mViewProj.reset();
  mDrawCalls.clear();

  ioRegistry.view<TransformComponent, CameraComponent>().each(
      [&](const TransformComponent& iTransform, const CameraComponent& iCamera) {
        if (iCamera.primary) {
          NE_ASSERT(!mViewProj, "At most one camera may be primary");
          mViewProj = iCamera.getViewProjectionMatrix(iTransform.getWorldMatrix(), iAspectRatio);
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
        const std::vector<Mesh::Submesh>& submeshes = iMesh.mesh->getSubmeshes();
        const size_t drawnCount = math::min(submeshes.size(), iMesh.materials.size());
        for (size_t i = 0; i < drawnCount; ++i) {
          const Material* material = iMesh.materials[i].get();
          if (material && material->getPipeline()) {
            mDrawCalls.push_back(DrawCall{.pipeline = material->getPipeline(),
                                          .submesh = &submeshes[i],
                                          .transform = iTransform.getWorldMatrix(),
                                          .color = iMesh.colorTint,
                                          .textureIndex = material->getTextureIndex(),
                                          .samplerIndex = static_cast<uint32_t>(material->getSamplerType())});
          }
        }
      });

  // Adjacent draws of the same submesh become one instanced indirect draw
  std::sort(mDrawCalls.begin(), mDrawCalls.end(), [](const DrawCall& iA, const DrawCall& iB) {
    if (iA.pipeline != iB.pipeline) {
      return iA.pipeline < iB.pipeline;
    }
    return iA.submesh < iB.submesh;
  });
}

void SceneRenderer::record(VkCommandBuffer iCommandBuffer, UploadAllocator& ioUploadAllocator) {
  if (!mViewProj || mDrawCalls.empty()) {
    return;
  }

  NE_ASSERT(iCommandBuffer != VK_NULL_HANDLE);

  mGeometryAllocator->bindIndexBuffer(iCommandBuffer);

  gpu::GlobalUniforms globalUniforms;
  globalUniforms.viewProj = *mViewProj;
  VkDeviceAddress globalUniformsAddr = ioUploadAllocator.upload(&globalUniforms, sizeof(gpu::GlobalUniforms)).address;

  size_t i = 0;
  Pipeline* currentPipeline = nullptr;
  while (i < mDrawCalls.size()) {
    if (currentPipeline != mDrawCalls[i].pipeline) {
      currentPipeline = mDrawCalls[i].pipeline;
      currentPipeline->bind(iCommandBuffer);
    }

    std::vector<gpu::DrawInfo> drawInfos;
    std::vector<gpu::InstanceData> instanceData;
    std::vector<VkDrawIndexedIndirectCommand> indirectCommands;

    while (i < mDrawCalls.size() && mDrawCalls[i].pipeline == currentPipeline) {
      const Mesh::Submesh* currentSubmesh = mDrawCalls[i].submesh;
      uint32_t startInstanceOffset = static_cast<uint32_t>(instanceData.size());

      uint32_t instanceCount = 0;
      while (i < mDrawCalls.size() && mDrawCalls[i].pipeline == currentPipeline && mDrawCalls[i].submesh == currentSubmesh) {
        Mat4 normalMatrix = mDrawCalls[i].transform.inversed().transposed();
        instanceData.push_back(gpu::InstanceData{.modelMatrix = mDrawCalls[i].transform,
                                                 .normalMatrix = normalMatrix,
                                                 .color = mDrawCalls[i].color,
                                                 .textureIndex = mDrawCalls[i].textureIndex,
                                                 .samplerIndex = mDrawCalls[i].samplerIndex});
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

    vkCmdPushConstants(iCommandBuffer, currentPipeline->getPipelineLayout(), gpu::kPushConstantStages, 0,
                       sizeof(gpu::ScenePushConstants), &pc);

    vkCmdDrawIndexedIndirect(iCommandBuffer, indirectAllocation.buffer, indirectAllocation.offset, numUniqueSubmeshes,
                             sizeof(VkDrawIndexedIndirectCommand));
  }
}

} // namespace ne
