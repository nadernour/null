#pragma once

#include <cstdint>
#include <memory>
#include <volk/volk.h>

namespace ne {

class Device;
class Pipeline;

// Maps the HDR scene color to display values (Khronos PBR Neutral), then encodes sRGB unless the attachment format does.
// Debug views skip PBR Neutral, so their values reach the screen unchanged.
class Tonemapper {
public:
  // iColorFormat is the format of the attachment it draws into
  Tonemapper(Device* iDevice, VkPipelineLayout iPipelineLayout, VkFormat iColorFormat);
  ~Tonemapper();

  Tonemapper(const Tonemapper&) = delete;
  Tonemapper& operator=(const Tonemapper&) = delete;
  Tonemapper(Tonemapper&&) = delete;
  Tonemapper& operator=(Tonemapper&&) = delete;

  // Draws one fullscreen triangle into the current pass, which must be the HDR image's size. The caller opens the pass,
  // sets the viewport, and binds the bindless set.
  void record(VkCommandBuffer iCommandBuffer, uint32_t iHdrTextureIndex, bool iEncodeSrgb, bool iApplyCurve);

private:
  std::unique_ptr<Pipeline> mPipeline;
};

} // namespace ne
