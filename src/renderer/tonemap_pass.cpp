#include "renderer/tonemap_pass.h"
#include "core/assert.h"
#include "renderer/device.h"
#include "renderer/gpu_types.h"
#include "renderer/pipeline.h"

namespace ne {

TonemapPass::TonemapPass(const Config& iConfig) {
  NE_ASSERT(iConfig.device && iConfig.pipelineLayout != VK_NULL_HANDLE && iConfig.colorFormat != VK_FORMAT_UNDEFINED);

  Pipeline::Config pipelineConfig{
      .shaderName = "tonemap",
      .layout = iConfig.pipelineLayout,
      .colorAttachmentFormat = iConfig.colorFormat,
      .depthMode = Pipeline::DepthMode::Disabled,
      .cullMode = VK_CULL_MODE_NONE, // The fullscreen triangle winds clockwise
  };
  mPipeline = std::make_unique<Pipeline>(iConfig.device->getDevice(), pipelineConfig);
}

TonemapPass::~TonemapPass() = default;

void TonemapPass::record(VkCommandBuffer iCommandBuffer, uint32_t iHdrTextureIndex, bool iEncodeSrgb) {
  mPipeline->bind(iCommandBuffer);

  gpu::TonemapConstants constants{.hdrTexture = iHdrTextureIndex, .encodeSrgb = iEncodeSrgb ? 1u : 0u};
  vkCmdPushConstants(iCommandBuffer, mPipeline->getPipelineLayout(), gpu::kPushConstantStages, 0,
                     sizeof(gpu::TonemapConstants), &constants);

  vkCmdDraw(iCommandBuffer, 3, 1, 0, 0);
}

} // namespace ne
