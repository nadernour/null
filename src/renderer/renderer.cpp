#include "renderer/renderer.h"
#include "components/camera_component.h"
#include "components/transform_component.h"
#include "core/assert.h"
#include "core/ecs.h"
#include "core/logger.h"
#include "platform/window.h"
#include "renderer/bindless_manager.h"
#include "renderer/device.h"
#include "renderer/frame_manager.h"
#include "renderer/gpu_types.h"
#include "renderer/image.h"
#include "renderer/imgui_manager.h"
#include "renderer/instance.h"
#include "renderer/resource_manager.h"
#include "renderer/sampler_manager.h"
#include "renderer/scene_renderer.h"
#include "renderer/shadow_cascades.h"
#include "renderer/tonemapper.h"
#include "renderer/utils.h"

#include <vulkan/utility/vk_format_utils.h>

#include <array>
#include <optional>

namespace ne {

namespace {

// Draws land in iRect of the attachments: all of them, or one tile of the shadow atlas
void setViewportAndScissor(VkCommandBuffer iCommandBuffer, const VkRect2D& iRect) {
  const VkViewport viewport{
      .x = static_cast<float>(iRect.offset.x),
      .y = static_cast<float>(iRect.offset.y),
      .width = static_cast<float>(iRect.extent.width),
      .height = static_cast<float>(iRect.extent.height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  vkCmdSetViewport(iCommandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(iCommandBuffer, 0, 1, &iRect);
}

// The primary camera, copied out of the registry
struct PrimaryCamera {
  CameraComponent camera;
  Mat4 world;
};

std::optional<PrimaryCamera> findPrimaryCamera(Registry& ioRegistry) {
  std::optional<PrimaryCamera> primary;
  ioRegistry.view<TransformComponent, CameraComponent>().each(
      [&](const TransformComponent& iTransform, const CameraComponent& iCamera) {
        if (iCamera.primary) {
          NE_ASSERT(!primary, "At most one camera may be primary");
          primary = PrimaryCamera{.camera = iCamera, .world = iTransform.getWorldMatrix()};
        }
      });
  return primary;
}

SceneView makeCameraView(const PrimaryCamera& iPrimary, float iAspectRatio, gpu::DebugView iDebugView) {
  const Mat4& world = iPrimary.world;
  // An orthographic camera's rays are parallel, as if its eye were infinitely far behind it: the point at w = 0 in its
  // backward direction. Every pixel then gets the same view direction.
  const Vec3 forward = world[0].xyz().getSafeNormal();
  const bool isOrthographic = iPrimary.camera.projectionType == CameraComponent::ProjectionType::Orthographic;
  return SceneView{
      .viewProj = iPrimary.camera.getViewProjectionMatrix(world, iAspectRatio),
      .eye = isOrthographic ? Vec4(-forward, 0.0f) : Vec4(world[3].xyz(), 1.0f),
      .exposure = iPrimary.camera.getExposure(),
      .debugView = iDebugView,
  };
}

// How far shadow lookups move off the surface, along its normal, in texels of their cascade. Each lookup compares against
// texels up to two away (nine taps, each blending four texels), so on a surface tilted by an angle a from the light its
// own stored depth there is up to 2 * tan(a) texels closer to the light than it is. The slope bias (SceneRenderer) clears
// 1.5 * tan(a) of that, and moving off by d along the normal d / cos(a), so d needs only 0.5 * sin(a): 1.5 covers every
// angle with room to spare, and gives a surface that faces the light, which has no slope, 1.5 texels of clearance.
constexpr float kShadowNormalOffsetTexels = 1.5f;

// Where cascade i is drawn in the atlas: tile (i mod 2, i / 2)
VkRect2D shadowTile(uint32_t iCascade, uint32_t iTileSize) {
  return VkRect2D{
      .offset = {static_cast<int32_t>((iCascade % 2) * iTileSize), static_cast<int32_t>((iCascade / 2) * iTileSize)},
      .extent = {iTileSize, iTileSize},
  };
}

// What color views shade with: each cascade's matrix into its tile of the atlas, the tiles' bounds, the camera's depth
// plane, which picks a cascade, and how far each cascade's lookups move off the surface
gpu::ShadowData makeShadowData(const ShadowCascades& iCascades, const Mat4& iCameraWorld, uint32_t iAtlasTexture,
                               uint32_t iAtlasSize, uint32_t iTileSize) {
  const float atlasSize = static_cast<float>(iAtlasSize);
  gpu::ShadowData data{.atlasTexture = iAtlasTexture, .texelSize = 1.0f / atlasSize};

  const Vec3 position = iCameraWorld[3].xyz();
  const Vec3 forward = iCameraWorld[0].xyz().getSafeNormal();
  data.depthPlane = Vec4(forward, -forward.dot(position));

  // Clip space runs from -1 to 1 across a tile, so half a tile's share of the atlas scales it, and the tile's center
  // offsets it
  const float scale = 0.5f * static_cast<float>(iTileSize) / atlasSize;
  const float halfTexel = 0.5f / atlasSize;
  for (uint32_t i = 0; i < ShadowCascades::kCount; ++i) {
    const VkRect2D tile = shadowTile(i, iTileSize);
    const float minU = static_cast<float>(tile.offset.x) / atlasSize;
    const float minV = static_cast<float>(tile.offset.y) / atlasSize;
    Mat4 toAtlas(1.0f);
    toAtlas[0].x = scale;
    toAtlas[1].y = scale;
    toAtlas[3].x = minU + scale;
    toAtlas[3].y = minV + scale;
    data.cascades[i] = toAtlas * iCascades.viewProj[i];
    data.tileBounds[i] =
        Vec4(minU + halfTexel, minV + halfTexel, minU + 2.0f * scale - halfTexel, minV + 2.0f * scale - halfTexel);
    data.splitDepths[i] = iCascades.splitDepths[i];
    data.normalOffsets[i] = iCascades.texelSizes[i] * kShadowNormalOffsetTexels;
  }
  return data;
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

  mResourceManager = std::make_unique<ResourceManager>(mDevice.get());

  mImGuiManager = std::make_unique<ImGuiManager>(mWindow, mInstance.get(), mDevice.get(), mFrameManager.get());

  // SceneRenderer's pipelines draw into the scene targets, so they share these formats
  mDepthFormat = mDevice->findDepthFormat();
  mSceneRenderer = std::make_unique<SceneRenderer>(mDevice.get(), mResourceManager.get(), kSceneColorFormat, mDepthFormat);

  const VkFormat swapchainFormat = mFrameManager->getColorFormat();
  mTonemapper = std::make_unique<Tonemapper>(mDevice.get(), mResourceManager->getPipelineLayout(), swapchainFormat);
  mEncodeSrgb = !vkuFormatIsSRGB(swapchainFormat);

  createTargets(mFrameManager->getExtent());
  mSceneColorTextureIndex = mResourceManager->getBindlessManager()->registerSampledImage(mSceneColor->getImageView());

  // The shadow pipelines draw into it too, so it takes the depth format. Its size never changes, so its slot is written
  // once.
  Image::Config shadowAtlasConfig{
      .width = kShadowAtlasSize,
      .height = kShadowAtlasSize,
      .format = mDepthFormat,
      .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      .debugName = "Shadow_Atlas",
  };
  mShadowAtlas = std::make_unique<Image>(mDevice.get(), shadowAtlasConfig);
  mShadowAtlasTextureIndex = mResourceManager->getBindlessManager()->registerSampledImage(mShadowAtlas->getImageView());
}

Renderer::~Renderer() {
  // Reverse order of creation
  mShadowAtlas.reset();
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

  // CPU only, so it runs before beginFrame waits for the frame's fence. Without a primary camera the scene pass only
  // clears.
  std::optional<SceneView> cameraView;
  std::optional<gpu::ShadowData> shadowData;
  std::array<SceneView, ShadowCascades::kCount> cascadeViews;
  if (const std::optional<PrimaryCamera> primary = findPrimaryCamera(ioRegistry)) {
    const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    cameraView = makeCameraView(*primary, aspectRatio, mDebugView);
    mSceneRenderer->prepare(ioRegistry, *cameraView);

    // The cascades follow the camera, so they're computed every frame
    if (const std::optional<SceneRenderer::ShadowLight>& shadowLight = mSceneRenderer->getShadowLight()) {
      const ShadowCascades::Config cascadesConfig{.distance = mShadowDistance, .resolution = kShadowTileSize};
      if (const std::optional<ShadowCascades> cascades =
              ShadowCascades::compute(primary->camera, primary->world, aspectRatio, shadowLight->world, cascadesConfig)) {
        const uint32_t atlasTexture = mShadowAtlasTextureIndex | (SamplerManager::kShadowSamplerIndex << 16);
        shadowData = makeShadowData(*cascades, primary->world, atlasTexture, kShadowAtlasSize, kShadowTileSize);
        for (uint32_t i = 0; i < ShadowCascades::kCount; ++i) {
          cascadeViews[i] = SceneView{.kind = SceneView::Kind::Depth, .viewProj = cascades->viewProj[i]};
        }
      }
    }
  }

  VkCommandBuffer commandBuffer = mFrameManager->beginFrame();
  if (commandBuffer == VK_NULL_HANDLE) {
    return;
  }

  // One layout for every graphics pipeline, so the set stays bound across passes
  mResourceManager->getBindlessManager()->bind(commandBuffer, mResourceManager->getPipelineLayout());

  if (cameraView) {
    UploadAllocator& uploadAllocator = *mFrameManager->getUploadAllocator();
    mSceneRenderer->upload(uploadAllocator);
    if (shadowData) {
      cameraView->shadows = uploadAllocator.upload(&*shadowData, sizeof(gpu::ShadowData)).address;
      recordShadowPass(commandBuffer, cascadeViews);
    }
  }

  recordScenePass(commandBuffer, extent, cameraView ? &*cameraView : nullptr);
  recordSwapchainPass(commandBuffer, extent);

  mFrameManager->endFrame();
}

void Renderer::waitIdle() const { mDevice->waitIdle(); }

void Renderer::recordShadowPass(VkCommandBuffer iCommandBuffer, std::span<const SceneView> iCascadeViews) {
  mShadowAtlas->transition(iCommandBuffer, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);

  // Every cascade draws into its own tile, so one clear covers them all
  VkRenderingAttachmentInfo depthAttachmentInfo{};
  depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  depthAttachmentInfo.imageView = mShadowAtlas->getImageView();
  depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depthAttachmentInfo.clearValue.depthStencil = {0.0f, 0}; // Reverse-Z: 0 is the far plane

  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = {.offset = {0, 0}, .extent = {kShadowAtlasSize, kShadowAtlasSize}};
  renderingInfo.layerCount = 1;
  renderingInfo.pDepthAttachment = &depthAttachmentInfo;

  vkCmdBeginRendering(iCommandBuffer, &renderingInfo);
  for (uint32_t i = 0; i < iCascadeViews.size(); ++i) {
    setViewportAndScissor(iCommandBuffer, shadowTile(i, kShadowTileSize));
    mSceneRenderer->recordView(iCommandBuffer, *mFrameManager->getUploadAllocator(), iCascadeViews[i]);
  }
  vkCmdEndRendering(iCommandBuffer);
}

void Renderer::recordScenePass(VkCommandBuffer iCommandBuffer, VkExtent2D iExtent, const SceneView* iCameraView) {
  // The shadow atlas, when this frame drew it, turns readable in the same batch
  std::array<VkImageMemoryBarrier2, 3> barriers{
      mSceneColor->transitionBarrier(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                     VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT),
      mSceneDepth->transitionBarrier(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT),
  };
  size_t barrierCount = 2;
  if (iCameraView && iCameraView->shadows != 0) {
    barriers[barrierCount++] = mShadowAtlas->transitionBarrier(
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
  }
  vk_utils::pipelineBarrier(iCommandBuffer, {barriers.data(), barrierCount});

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
  if (iCameraView) {
    setViewportAndScissor(iCommandBuffer, renderingInfo.renderArea);
    mSceneRenderer->recordView(iCommandBuffer, *mFrameManager->getUploadAllocator(), *iCameraView);
  }
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
      vk_utils::makeImageBarrier(swapchainImage, colorSubresourceRange, VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_NONE,
                                 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                 FrameManager::kImageAcquiredStage, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT),
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
  setViewportAndScissor(iCommandBuffer, renderingInfo.renderArea);
  // The Cascades view is the lit image tinted, so it keeps the curve; the channel views show their values as they are
  const bool applyCurve = mDebugView == gpu::DebugView::Lit || mDebugView == gpu::DebugView::Cascades;
  mTonemapper->record(iCommandBuffer, mSceneColorTextureIndex, mEncodeSrgb, applyCurve);
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
