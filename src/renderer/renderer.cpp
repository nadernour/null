#include "renderer/renderer.h"
#include "core/assert.h"
#include "core/logger.h"
#include "platform/window.h"
#include "renderer/bindless_manager.h"
#include "renderer/device.h"
#include "renderer/frame_manager.h"
#include "renderer/image.h"
#include "renderer/imgui_manager.h"
#include "renderer/instance.h"
#include "renderer/resource_manager.h"
#include "renderer/scene_renderer.h"
#include "renderer/tonemapper.h"
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

Renderer::Renderer(Window* iWindow, const std::string& iAppName) : mWindow(iWindow) {
  NE_ASSERT(mWindow);

  Instance::Config instanceConfig{
      .appName = iAppName,
      .requiredExtensions = mWindow->getRequiredInstanceExtensions(),
  };
  mInstance = std::make_unique<Instance>(instanceConfig);

  VK_CHECK(mWindow->createSurface(mInstance->getInstance()));

  Device::Config deviceConfig{
      .surface = mWindow->getSurface(),
  };
  mDevice = std::make_unique<Device>(mInstance.get(), deviceConfig);

  mFrameManager = std::make_unique<FrameManager>(mDevice.get(), mWindow);

  // The pipelines ResourceManager creates draw into the scene targets, so they share these formats
  mDepthFormat = mDevice->findDepthFormat();
  mResourceManager = std::make_unique<ResourceManager>(mDevice.get(), kSceneColorFormat, mDepthFormat);

  mImGuiManager = std::make_unique<ImGuiManager>(mWindow, mInstance.get(), mDevice.get(), mFrameManager.get());

  mSceneRenderer = std::make_unique<SceneRenderer>(mResourceManager->getGeometryAllocator());

  const VkFormat swapchainFormat = mFrameManager->getColorFormat();
  mTonemapper = std::make_unique<Tonemapper>(mDevice.get(), mResourceManager->getPipelineLayout(), swapchainFormat);
  mEncodeSrgb = !vkuFormatIsSRGB(swapchainFormat);

  createTargets(mFrameManager->getExtent());
  mSceneColorTextureIndex = mResourceManager->getBindlessManager()->registerSampledImage(mSceneColor->getImageView());
}

Renderer::~Renderer() {
  // Reverse order of creation
  mSceneDepth.reset();
  mSceneColor.reset();
  mTonemapper.reset();
  mSceneRenderer.reset();
  mImGuiManager.reset();
  mResourceManager.reset();
  mFrameManager.reset();
  mDevice.reset();

  mWindow->destroySurface(mInstance->getInstance());

  mInstance.reset();
}

void Renderer::render(Registry& ioRegistry) {
  // Everything created since the last frame reaches the GPU before this frame's draws read it
  mResourceManager->flushUploads();

  // Checked before beginFrame, so the wait for idle never happens while a frame is recording
  const VkExtent2D extent = mFrameManager->getExtent();
  if (extent.width != mSceneColor->getConfig().width || extent.height != mSceneColor->getConfig().height) {
    mDevice->waitIdle(); // No submitted frame may still read the HDR image's slot
    createTargets(extent);
    mResourceManager->getBindlessManager()->updateSampledImage(mSceneColorTextureIndex, mSceneColor->getImageView());
    NE_VERBOSE("Renderer: Scene targets resized to {}x{}", extent.width, extent.height);
  }

  // CPU only, so it runs before beginFrame waits for the frame's fence. The upload buffer grows to the size it returns
  // before anything is written to it.
  const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
  const VkDeviceSize uploadSize = mSceneRenderer->prepare(ioRegistry, aspectRatio);

  VkCommandBuffer commandBuffer = mFrameManager->beginFrame();
  if (commandBuffer == VK_NULL_HANDLE) {
    return;
  }
  mFrameManager->reserveUploadBuffer(uploadSize);

  // One layout for every graphics pipeline, so the set stays bound across passes
  mResourceManager->getBindlessManager()->bind(commandBuffer, mResourceManager->getPipelineLayout());

  recordScenePass(commandBuffer, extent);
  recordSwapchainPass(commandBuffer, extent);

  mFrameManager->endFrame();
}

void Renderer::waitIdle() const { mDevice->waitIdle(); }

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

  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = {.offset = {0, 0}, .extent = iExtent};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachmentInfo;
  renderingInfo.pDepthAttachment = &depthAttachmentInfo;

  vkCmdBeginRendering(iCommandBuffer, &renderingInfo);
  setViewportAndScissor(iCommandBuffer, iExtent);
  mSceneRenderer->record(iCommandBuffer, *mFrameManager->getUploadBuffer());
  vkCmdEndRendering(iCommandBuffer);
}

void Renderer::recordSwapchainPass(VkCommandBuffer iCommandBuffer, VkExtent2D iExtent) {
  // Swapchain images aren't Images, so their barriers are written by hand. The tonemap writes every pixel, so the old
  // contents are discarded.
  const VkImage swapchainImage = mFrameManager->getActiveImage();
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
      vk_utils::makeImageBarrier(
          swapchainImage, colorSubresourceRange, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT),
  };
  vk_utils::pipelineBarrier(iCommandBuffer, barriers);

  VkRenderingAttachmentInfo colorAttachmentInfo{};
  colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  colorAttachmentInfo.imageView = mFrameManager->getActiveImageView();
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
  mTonemapper->record(iCommandBuffer, mSceneColorTextureIndex, mEncodeSrgb);
  mImGuiManager->draw(iCommandBuffer);
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
  mSceneColor = std::make_unique<Image>(mDevice.get(), colorConfig);

  Image::Config depthConfig{
      .width = iExtent.width,
      .height = iExtent.height,
      .format = mDepthFormat,
      .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      .debugName = "Scene_Depth",
  };
  mSceneDepth = std::make_unique<Image>(mDevice.get(), depthConfig);
}

} // namespace ne
