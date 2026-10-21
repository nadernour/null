#pragma once

#include <cstdint>
#include <memory>
#include <volk/volk.h>

namespace ne {

class Device;
class Pipeline;

// Maps the HDR scene color to display values (Khronos PBR Neutral), then encodes sRGB unless the attachment format does
class TonemapPass {
public:
  struct Config {
    Device* device = nullptr;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED; // Of the attachment it draws into
  };

  TonemapPass(const Config& iConfig);
  ~TonemapPass();

  TonemapPass(const TonemapPass&) = delete;
  TonemapPass& operator=(const TonemapPass&) = delete;
  TonemapPass(TonemapPass&&) = delete;
  TonemapPass& operator=(TonemapPass&&) = delete;

  // Draws one fullscreen triangle into the current rendering scope, which must be the HDR image's size. The caller opens
  // the scope, sets the viewport, and binds the bindless set.
  void record(VkCommandBuffer iCommandBuffer, uint32_t iHdrTextureIndex, bool iEncodeSrgb);

private:
  std::unique_ptr<Pipeline> mPipeline;
};

} // namespace ne
