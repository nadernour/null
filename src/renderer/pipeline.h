#pragma once

#include <string>
#include <vector>
#include <volk/volk.h>

namespace ne {

class Pipeline {
public:
  enum class DepthMode : uint8_t {
    Disabled,  // UI / 2D overlays
    ReadWrite, // Standard reverse-Z (internally maps to VK_COMPARE_OP_GREATER_OR_EQUAL)
    ReadOnly   // Post-passes, transparents, or decals
  };

  enum class BlendMode : uint8_t {
    Disabled,
    Alpha // Transparent surfaces: color x alpha + what's behind x (1 - alpha)
  };

  // The cull mode isn't part of it: it's dynamic state, which whoever records a draw sets with vkCmdSetCullMode
  struct Config {
    std::string shaderName;
    std::string fragmentEntry = "fragMain";
    std::string debugName; // Required; with no default, GCC warns about a Config that leaves it out
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkFormat colorAttachmentFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    DepthMode depthMode = DepthMode::ReadWrite;
    BlendMode blendMode = BlendMode::Disabled;
  };

  static VkPipelineLayout createPipelineLayout(VkDevice iDevice, const std::vector<VkDescriptorSetLayout>& iDescriptorSetLayouts,
                                               const std::vector<VkPushConstantRange>& iPushConstantRanges,
                                               const std::string& iDebugName = "");
  static void destroyPipelineLayout(VkDevice iDevice, VkPipelineLayout iPipelineLayout);

  Pipeline(VkDevice iDevice, const Config& iConfig);
  ~Pipeline();

  Pipeline(const Pipeline&) = delete;
  Pipeline& operator=(const Pipeline&) = delete;

  void bind(VkCommandBuffer iCommandBuffer);

  VkPipeline getPipeline() const { return mGraphicsPipeline; }
  VkPipelineLayout getPipelineLayout() const { return mPipelineLayout; }

private:
  VkShaderModule createShaderModule(const std::string& iFilename);

  VkDevice mDevice = VK_NULL_HANDLE;
  VkPipeline mGraphicsPipeline = VK_NULL_HANDLE;
  VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
};

} // namespace ne
