#include "renderer/tonemapper.h"
#include "core/assert.h"
#include "renderer/device.h"
#include "renderer/gpu_types.h"
#include "renderer/pipeline.h"

namespace ne {

Tonemapper::Tonemapper(Device* iDevice, VkPipelineLayout iPipelineLayout, VkFormat iColorFormat) {
  NE_ASSERT(iDevice && iPipelineLayout != VK_NULL_HANDLE && iColorFormat != VK_FORMAT_UNDEFINED);

  Pipeline::Config pipelineConfig{
      .shaderName = "tonemap",
      .layout = iPipelineLayout,
      .colorAttachmentFormat = iColorFormat,
      .depthMode = Pipeline::DepthMode::Disabled,
      .cullMode = VK_CULL_MODE_NONE, // The fullscreen triangle winds clockwise
  };
  mPipeline = std::make_unique<Pipeline>(iDevice->getDevice(), pipelineConfig);
}

Tonemapper::~Tonemapper() = default;

void Tonemapper::record(VkCommandBuffer iCommandBuffer, uint32_t iHdrTextureIndex, bool iEncodeSrgb) {
  mPipeline->bind(iCommandBuffer);

  gpu::TonemapPushConstants constants{.hdrTexture = iHdrTextureIndex, .encodeSrgb = iEncodeSrgb ? 1u : 0u};
  vkCmdPushConstants(iCommandBuffer, mPipeline->getPipelineLayout(), gpu::kPushConstantStages, 0,
                     sizeof(gpu::TonemapPushConstants), &constants);

  vkCmdDraw(iCommandBuffer, 3, 1, 0, 0);
}

} // namespace ne
