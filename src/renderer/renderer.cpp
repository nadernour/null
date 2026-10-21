#include "renderer/renderer.h"
#include "core/assert.h"
#include "core/logger.h"
#include "renderer/bindless_manager.h"
#include "renderer/device.h"
#include "renderer/frame_renderer.h"
#include "renderer/image.h"
#include "renderer/imgui_manager.h"
#include "renderer/resource_manager.h"
#include "renderer/scene_renderer.h"
#include "renderer/tonemap_pass.h"
#include "renderer/utils.h"

#include <vulkan/utility/vk_format_utils.h>

#include <array>

namespace ne {

namespace {

void setViewportAndScissor(VkCommandBuffer iCommandBuffer, VkExtent2D iExtent) {
  VkViewport viewport = {
      .x = 0.0f, .y = 0.0f, .width = (float)iExtent.width, .height = (float)iExtent.height, .minDepth = 0.0f, .maxDepth = 1.0f};
  VkRect2D scissor = {.offset = {0, 0}, .extent = iExtent};
  vkCmdSetViewport(iCommandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(iCommandBuffer, 0, 1, &scissor);
}

} // namespace

Renderer::Renderer(const Config& iConfig)
    : mDevice(iConfig.device), mFrameRenderer(iConfig.frameRenderer), mResourceManager(iConfig.resourceManager),
      mDepthFormat(iConfig.depthFormat) {
  NE_ASSERT(mDevice && mFrameRenderer && mResourceManager && mDepthFormat != VK_FORMAT_UNDEFINED);

  SceneRenderer::Config sceneRendererConfig{
      .geometryAllocator = mResourceManager->getGeometryAllocator(),
  };
  mSceneRenderer = std::make_unique<SceneRenderer>(sceneRendererConfig);

  const VkFormat swapchainFormat = mFrameRenderer->getColorFormat();
  TonemapPass::Config tonemapConfig{
      .device = mDevice,
      .pipelineLayout = mResourceManager->getPipelineLayout(),
      .colorFormat = swapchainFormat,
  };
  mTonemapPass = std::make_unique<TonemapPass>(tonemapConfig);
  mEncodeSrgb = !vkuFormatIsSRGB(swapchainFormat);

  createTargets(mFrameRenderer->getExtent());
  mSceneColorTextureIndex = mResourceManager->getBindlessManager()->registerSampledImage(mSceneColor->getImageView());
}

Renderer::~Renderer() = default;

void Renderer::render(Registry* iRegistry, ImGuiManager* iGuiManager) {
  // Everything created since the last frame reaches the GPU before this frame's draws read it
  mResourceManager->flushUploads();

  if (!iRegistry) {
    return;
  }

  // Checked before beginFrame, so the wait for idle never happens while a frame is recording
  const VkExtent2D extent = mFrameRenderer->getExtent();
  if (extent.width != mSceneColor->getConfig().width || extent.height != mSceneColor->getConfig().height) {
    mDevice->waitIdle(); // No submitted frame may still read the HDR image's slot
    createTargets(extent);
    mResourceManager->getBindlessManager()->updateSampledImage(mSceneColorTextureIndex, mSceneColor->getImageView());
    NE_VERBOSE("Renderer: Scene targets resized to {}x{}", extent.width, extent.height);
  }

  // CPU only, so it runs before beginFrame waits for the frame's fence. The upload buffer grows to the size it returns
  // before anything is written to it.
  const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
  const VkDeviceSize uploadSize = mSceneRenderer->prepare(iRegistry, aspectRatio);

  VkCommandBuffer commandBuffer = mFrameRenderer->beginFrame();
  if (commandBuffer == VK_NULL_HANDLE) {
    return;
  }
  mFrameRenderer->reserveUploadBuffer(uploadSize);

  // One layout for every graphics pipeline, so the set stays bound across passes
  mResourceManager->getBindlessManager()->bind(commandBuffer, mResourceManager->getPipelineLayout());

  recordScenePass(commandBuffer, extent);
  recordSwapchainPass(commandBuffer, extent, iGuiManager);

  mFrameRenderer->endFrame();
}

void Renderer::recordScenePass(VkCommandBuffer iCommandBuffer, VkExtent2D iExtent) {
  const std::array barriers{
      mSceneColor->transitionBarrier(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                     VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT),
      mSceneDepth->transitionBarrier(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT),
  };
  vk_utils::pipelineBarrier(iCommandBuffer, barriers);

  VkRenderingAttachmentInfo colorAttachmentInfo{};
  colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  colorAttachmentInfo.imageView = mSceneColor->getImageView();
  colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachmentInfo.clearValue = {0.1f, 0.1f, 0.1f, 1.0f};

  VkRenderingAttachmentInfo depthAttachmentInfo{};
  depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  depthAttachmentInfo.imageView = mSceneDepth->getImageView();
  depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachmentInfo.clearValue.depthStencil = {0.0f, 0}; // Reverse-Z: 0 is the far plane

  // Pipelines declare a stencil attachment whenever the depth format has stencil, so the scope must too
  const bool hasStencil = (mSceneDepth->getSubresourceRange().aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;

  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = {.offset = {0, 0}, .extent = iExtent};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachmentInfo;
  renderingInfo.pDepthAttachment = &depthAttachmentInfo;
  renderingInfo.pStencilAttachment = hasStencil ? &depthAttachmentInfo : nullptr;

  vkCmdBeginRendering(iCommandBuffer, &renderingInfo);
  setViewportAndScissor(iCommandBuffer, iExtent);
  mSceneRenderer->record(iCommandBuffer, *mFrameRenderer->getUploadBuffer());
  vkCmdEndRendering(iCommandBuffer);
}

void Renderer::recordSwapchainPass(VkCommandBuffer iCommandBuffer, VkExtent2D iExtent, ImGuiManager* iGuiManager) {
  // Swapchain images aren't Images, so their barriers are written by hand. The tonemap writes every pixel, so the old
  // contents are discarded.
  const VkImage swapchainImage = mFrameRenderer->getActiveImage();
  const VkImageSubresourceRange colorSubresourceRange{
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
  };
  const std::array barriers{
      mSceneColor->transitionBarrier(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT),
      vk_utils::makeImageBarrier(swapchainImage, colorSubresourceRange, VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_NONE,
                                 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT),
  };
  vk_utils::pipelineBarrier(iCommandBuffer, barriers);

  VkRenderingAttachmentInfo colorAttachmentInfo{};
  colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  colorAttachmentInfo.imageView = mFrameRenderer->getActiveImageView();
  colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = {.offset = {0, 0}, .extent = iExtent};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachmentInfo;

  vkCmdBeginRendering(iCommandBuffer, &renderingInfo);
  setViewportAndScissor(iCommandBuffer, iExtent);
  mTonemapPass->record(iCommandBuffer, mSceneColorTextureIndex, mEncodeSrgb);
  if (iGuiManager) {
    iGuiManager->draw(iCommandBuffer);
  }
  vkCmdEndRendering(iCommandBuffer);

  vk_utils::transitionImageLayout(iCommandBuffer, swapchainImage, colorSubresourceRange, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE,
                                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
}

void Renderer::createTargets(VkExtent2D iExtent) {
  Image::Config colorConfig{
      .width = iExtent.width,
      .height = iExtent.height,
      .format = kSceneColorFormat,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      .debugName = "Scene_Color",
  };
  mSceneColor = std::make_unique<Image>(mDevice, colorConfig);

  Image::Config depthConfig{
      .width = iExtent.width,
      .height = iExtent.height,
      .format = mDepthFormat,
      .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      .debugName = "Scene_Depth",
  };
  mSceneDepth = std::make_unique<Image>(mDevice, depthConfig);
}

} // namespace ne
