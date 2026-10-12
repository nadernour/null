#include "renderer/scene_renderer.h"
#include "components/camera_component.h"
#include "components/mesh_component.h"
#include "components/transform_component.h"
#include "core/assert.h"
#include "core/ecs.h"
#include "renderer/bindless_manager.h"
#include "renderer/buffer.h"
#include "renderer/device.h"
#include "renderer/frame_renderer.h"
#include "renderer/geometry_allocator.h"
#include "renderer/gpu_types.h"
#include "renderer/image.h"
#include "renderer/imgui_manager.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/pipeline.h"
#include "renderer/utils.h"

// std
#include <algorithm>
#include <optional>

namespace ne {

SceneRenderer::SceneRenderer(const Config& iConfig)
    : mDevice(iConfig.device), mGeometryAllocator(iConfig.geometryAllocator), mBindlessManager(iConfig.bindlessManager),
      mScenePipelineLayout(iConfig.scenePipelineLayout), mFrameRenderer(iConfig.frameRenderer) {
  NE_ASSERT(mDevice && mGeometryAllocator && mBindlessManager && mScenePipelineLayout != VK_NULL_HANDLE && mFrameRenderer);
}

SceneRenderer::~SceneRenderer() = default;

void SceneRenderer::waitIdle() { mDevice->waitIdle(); }

void SceneRenderer::render(VkCommandBuffer iCommandBuffer, Registry* iRegistry, ImGuiManager* iGuiManager) {
  if (!iRegistry) {
    return;
  }

  mDrawCalls.clear();

  VkExtent2D extent = mFrameRenderer->getExtent();
  VkImage activeColorImage = mFrameRenderer->getActiveImage();
  VkImageView activeColorImageView = mFrameRenderer->getActiveImageView();
  Image* depthImage = mFrameRenderer->getDepthImage();
  NE_ASSERT(depthImage, "Depth image must not be null");

  VkImageSubresourceRange colorSubresourceRange{
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
  };
  vk_utils::transitionImageLayout(iCommandBuffer, activeColorImage, colorSubresourceRange, VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_NONE,
                                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

  vk_utils::transitionImageLayout(iCommandBuffer, depthImage->getImage(), depthImage->getSubresourceRange(),
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);

  VkRenderingAttachmentInfo colorAttachmentInfo{};
  colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  colorAttachmentInfo.imageView = activeColorImageView;
  colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachmentInfo.clearValue = {0.1f, 0.1f, 0.1f, 1.0f};

  VkRenderingAttachmentInfo depthAttachmentInfo{};
  depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  depthAttachmentInfo.imageView = depthImage->getImageView();
  depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachmentInfo.clearValue.depthStencil = {0.0f, 0};

  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = {.offset = {0, 0}, .extent = extent};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachmentInfo;
  renderingInfo.pDepthAttachment = &depthAttachmentInfo;
  renderingInfo.pStencilAttachment = &depthAttachmentInfo;

  vkCmdBeginRendering(iCommandBuffer, &renderingInfo);

  VkViewport viewport = {
      .x = 0.0f, .y = 0.0f, .width = (float)extent.width, .height = (float)extent.height, .minDepth = 0.0f, .maxDepth = 1.0f};
  VkRect2D scissor = {.offset = {0, 0}, .extent = extent};
  vkCmdSetViewport(iCommandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(iCommandBuffer, 0, 1, &scissor);

  mGeometryAllocator->bindIndexBuffer(iCommandBuffer);

  mBindlessManager->bind(iCommandBuffer, mScenePipelineLayout);

  const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
  std::optional<Mat4> viewProj;
  iRegistry->view<TransformComponent, CameraComponent>().each(
      [&](const TransformComponent& iTransform, const CameraComponent& iCamera) {
        if (iCamera.primary) {
          NE_ASSERT(!viewProj, "At most one camera may be primary");
          viewProj = iCamera.getViewProjectionMatrix(iTransform.getWorldMatrix(), aspectRatio);
        }
      });

  iRegistry->view<TransformComponent, MeshComponent>().each(
      [&](Entity, const TransformComponent& iTransform, const MeshComponent& iMesh) {
        if (!iMesh.mesh) {
          return;
        }
        const std::vector<Mesh::Submesh>& submeshes = iMesh.mesh->getSubmeshes();
        const size_t drawnCount = std::min(submeshes.size(), iMesh.materials.size());
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

  // Without a primary camera only the UI is drawn
  if (viewProj) {
    submit(iCommandBuffer, *viewProj);
  }

  if (iGuiManager) {
    iGuiManager->draw(iCommandBuffer);
  }

  vkCmdEndRendering(iCommandBuffer);

  vk_utils::transitionImageLayout(iCommandBuffer, activeColorImage, colorSubresourceRange, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE,
                                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
}

void SceneRenderer::draw(Registry* iRegistry, ImGuiManager* iGuiManager) {
  if (!iRegistry) {
    return;
  }

  VkCommandBuffer commandBuffer = mFrameRenderer->beginFrame();
  if (commandBuffer == VK_NULL_HANDLE) {
    return;
  }

  render(commandBuffer, iRegistry, iGuiManager);

  mFrameRenderer->endFrame();
}

void SceneRenderer::submit(VkCommandBuffer iCommandBuffer, const Mat4& iViewProj) {
  if (mDrawCalls.empty())
    return;

  NE_ASSERT(iCommandBuffer != VK_NULL_HANDLE);

  // Adjacent draws of the same submesh become one instanced indirect draw
  std::sort(mDrawCalls.begin(), mDrawCalls.end(), [](const DrawCall& iA, const DrawCall& iB) {
    if (iA.pipeline != iB.pipeline) {
      return iA.pipeline < iB.pipeline;
    }
    return iA.submesh < iB.submesh;
  });

  // +16 per upload covers its worst-case alignment padding
  VkDeviceSize totalRequiredSize = sizeof(gpu::GlobalUniforms) + 16;
  Pipeline* lastPipeline = nullptr;
  const Mesh::Submesh* lastSubmesh = nullptr;
  for (const auto& call : mDrawCalls) {
    if (call.pipeline != lastPipeline) {
      lastPipeline = call.pipeline;
      lastSubmesh = nullptr;
    }
    if (call.submesh != lastSubmesh) {
      lastSubmesh = call.submesh;
      totalRequiredSize += sizeof(gpu::DrawInfo) + 16;
      totalRequiredSize += sizeof(VkDrawIndexedIndirectCommand) + 16;
    }
    totalRequiredSize += sizeof(gpu::InstanceData) + 16;
  }

  Buffer* uploadBuffer = mFrameRenderer->getUploadBuffer();
  if (uploadBuffer->getConfig().size < totalRequiredSize) {
    VkDeviceSize newSize = std::max(totalRequiredSize, uploadBuffer->getConfig().size * 2);
    mFrameRenderer->recreateUploadBuffer(newSize);
    uploadBuffer = mFrameRenderer->getUploadBuffer();
  }

  gpu::GlobalUniforms globalUniforms;
  globalUniforms.viewProj = iViewProj;
  VkDeviceAddress globalUniformsAddr =
      uploadBuffer->getDeviceAddress(uploadBuffer->upload(&globalUniforms, sizeof(gpu::GlobalUniforms)));

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

    VkDeviceAddress drawInfosAddr =
        uploadBuffer->getDeviceAddress(uploadBuffer->upload(drawInfos.data(), drawInfos.size() * sizeof(gpu::DrawInfo)));
    VkDeviceAddress instancesAddr = uploadBuffer->getDeviceAddress(
        uploadBuffer->upload(instanceData.data(), instanceData.size() * sizeof(gpu::InstanceData)));
    VkDeviceSize indirectOffset =
        uploadBuffer->upload(indirectCommands.data(), indirectCommands.size() * sizeof(VkDrawIndexedIndirectCommand));

    gpu::PushConstants pc{};
    pc.drawInfos = drawInfosAddr;
    pc.globalUniforms = globalUniformsAddr;
    pc.instances = instancesAddr;

    vkCmdPushConstants(iCommandBuffer, currentPipeline->getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(gpu::PushConstants), &pc);

    vkCmdDrawIndexedIndirect(iCommandBuffer, uploadBuffer->getBuffer(), indirectOffset, numUniqueSubmeshes,
                             sizeof(VkDrawIndexedIndirectCommand));
  }
}

} // namespace ne
