#include "renderer/pipeline.h"
#include "core/assert.h"
#include "core/filesystem.h"
#include "core/platform.h"
#include "renderer/mesh.h"
#include "renderer/utils.h"

// std
#include <filesystem>
#include <fstream>

namespace ne {

Pipeline::Pipeline(VkDevice iDevice, const Config& iConfig) : mDevice(iDevice) {
  NE_ASSERT(mDevice != VK_NULL_HANDLE, "Device must not be VK_NULL_HANDLE");
  VkShaderModule shaderModule = createShaderModule(iConfig.shaderName);

  VkPipelineShaderStageCreateInfo vertShaderStageCreateInfo{};
  vertShaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageCreateInfo.module = shaderModule;
  vertShaderStageCreateInfo.pName = "vertMain";

  VkPipelineShaderStageCreateInfo fragShaderStageCreateInfo{};
  fragShaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageCreateInfo.module = shaderModule;
  fragShaderStageCreateInfo.pName = "fragMain";

  VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageCreateInfo, fragShaderStageCreateInfo};

  VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo{};
  vertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputStateCreateInfo.vertexBindingDescriptionCount = 0;
  vertexInputStateCreateInfo.pVertexBindingDescriptions = nullptr;
  vertexInputStateCreateInfo.vertexAttributeDescriptionCount = 0;
  vertexInputStateCreateInfo.pVertexAttributeDescriptions = nullptr;

  VkPipelineInputAssemblyStateCreateInfo inputAssemplyStateCreateInfo{};
  inputAssemplyStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemplyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportStateCreateInfo{};
  viewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportStateCreateInfo.viewportCount = 1;
  viewportStateCreateInfo.pViewports = nullptr;
  viewportStateCreateInfo.scissorCount = 1;
  viewportStateCreateInfo.pScissors = nullptr;

  VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo{};
  rasterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationStateCreateInfo.depthClampEnable = VK_FALSE; // enabling requires a gpu feature
  rasterizationStateCreateInfo.rasterizerDiscardEnable = VK_FALSE;
  rasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL; // changing requires a gpu feature
  rasterizationStateCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // Right-handed (glTF) winding
  rasterizationStateCreateInfo.depthBiasEnable = VK_FALSE;
  rasterizationStateCreateInfo.depthBiasConstantFactor = 0.0f;
  rasterizationStateCreateInfo.depthBiasClamp = 0.0f;
  rasterizationStateCreateInfo.depthBiasSlopeFactor = 0.0f;
  rasterizationStateCreateInfo.lineWidth = 1.0f; // Any value other than 1.0 requires "widelines" Gpu feature

  VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo{};
  multisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampleStateCreateInfo.sampleShadingEnable = VK_FALSE;
  multisampleStateCreateInfo.minSampleShading = 0.0f;
  multisampleStateCreateInfo.pSampleMask = nullptr;
  multisampleStateCreateInfo.alphaToCoverageEnable = VK_FALSE;
  multisampleStateCreateInfo.alphaToOneEnable = VK_FALSE;

  VkPipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo{};
  depthStencilStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencilStateCreateInfo.depthTestEnable = iConfig.depthMode != DepthMode::Disabled ? VK_TRUE : VK_FALSE;
  depthStencilStateCreateInfo.depthWriteEnable = iConfig.depthMode == DepthMode::ReadWrite ? VK_TRUE : VK_FALSE;
  depthStencilStateCreateInfo.depthBoundsTestEnable = VK_FALSE;
  depthStencilStateCreateInfo.minDepthBounds = 0.0f;
  depthStencilStateCreateInfo.maxDepthBounds = 1.0f;
  depthStencilStateCreateInfo.stencilTestEnable = iConfig.stencilMode != StencilMode::Disabled ? VK_TRUE : VK_FALSE;
  depthStencilStateCreateInfo.depthCompareOp =
      iConfig.depthMode != DepthMode::Disabled ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_ALWAYS;

  VkPipelineColorBlendAttachmentState colorBendAttachmentState{};
  colorBendAttachmentState.blendEnable = false;
  colorBendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
  colorBendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
  colorBendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT;

  VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo{};
  colorBlendStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendStateCreateInfo.logicOpEnable = VK_FALSE;
  colorBlendStateCreateInfo.logicOp = VK_LOGIC_OP_COPY;
  colorBlendStateCreateInfo.attachmentCount = 1;
  colorBlendStateCreateInfo.pAttachments = &colorBendAttachmentState;

  NE_ASSERT(iConfig.layout != VK_NULL_HANDLE, "Pipeline layout must not be VK_NULL_HANDLE");
  mPipelineLayout = iConfig.layout;

  std::vector<VkDynamicState> dynamicState = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo{};
  dynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicStateCreateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicState.size());
  dynamicStateCreateInfo.pDynamicStates = dynamicState.data();
  VkPipelineRenderingCreateInfo renderingCreateInfo{};
  renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  renderingCreateInfo.colorAttachmentCount = (iConfig.colorAttachmentFormat != VK_FORMAT_UNDEFINED) ? 1 : 0;
  renderingCreateInfo.pColorAttachmentFormats =
      (iConfig.colorAttachmentFormat != VK_FORMAT_UNDEFINED) ? &iConfig.colorAttachmentFormat : nullptr;
  renderingCreateInfo.depthAttachmentFormat = iConfig.depthAttachmentFormat;
  renderingCreateInfo.stencilAttachmentFormat =
      (vk_utils::deduceAspectFlags(iConfig.depthAttachmentFormat) & VK_IMAGE_ASPECT_STENCIL_BIT) ? iConfig.depthAttachmentFormat
                                                                                                 : VK_FORMAT_UNDEFINED;

  VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo{};
  graphicsPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  graphicsPipelineCreateInfo.pNext = &renderingCreateInfo;
  graphicsPipelineCreateInfo.stageCount = 2;
  graphicsPipelineCreateInfo.pStages = shaderStages;
  graphicsPipelineCreateInfo.pVertexInputState = &vertexInputStateCreateInfo;
  graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssemplyStateCreateInfo;
  graphicsPipelineCreateInfo.pTessellationState = nullptr;
  graphicsPipelineCreateInfo.pViewportState = &viewportStateCreateInfo;
  graphicsPipelineCreateInfo.pRasterizationState = &rasterizationStateCreateInfo;
  graphicsPipelineCreateInfo.pMultisampleState = &multisampleStateCreateInfo;
  graphicsPipelineCreateInfo.pDepthStencilState = &depthStencilStateCreateInfo;
  graphicsPipelineCreateInfo.pColorBlendState = &colorBlendStateCreateInfo;
  graphicsPipelineCreateInfo.pDynamicState = &dynamicStateCreateInfo;
  graphicsPipelineCreateInfo.layout = mPipelineLayout;
  graphicsPipelineCreateInfo.renderPass = VK_NULL_HANDLE;
  graphicsPipelineCreateInfo.subpass = 0;
  graphicsPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
  graphicsPipelineCreateInfo.basePipelineIndex = 0;

  VK_CHECK(vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &graphicsPipelineCreateInfo, nullptr, &mGraphicsPipeline));
  vk_utils::setDebugObjectName(mDevice, mGraphicsPipeline, std::format("{}_GraphicsPipeline", iConfig.shaderName).c_str());

  vkDestroyShaderModule(mDevice, shaderModule, nullptr);
  NE_VERBOSE("Created Graphics Pipeline for shader: '{}' (Pipeline: {}, Layout: {})", iConfig.shaderName,
             (void*)mGraphicsPipeline, (void*)mPipelineLayout);
}

Pipeline::~Pipeline() {
  NE_VERBOSE("Destroyed Graphics Pipeline (Pipeline: {}, Layout: {})", (void*)mGraphicsPipeline, (void*)mPipelineLayout);
  if (mGraphicsPipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(mDevice, mGraphicsPipeline, nullptr);
  }
}

VkPipelineLayout Pipeline::createPipelineLayout(VkDevice iDevice, const std::vector<VkDescriptorSetLayout>& iDescriptorSetLayouts,
                                                const std::vector<VkPushConstantRange>& iPushConstantRanges,
                                                const std::string& iDebugName) {
  NE_ASSERT(iDevice != VK_NULL_HANDLE, "Device must not be null");

  VkPipelineLayoutCreateInfo layoutCreateInfo{};
  layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(iDescriptorSetLayouts.size());
  layoutCreateInfo.pSetLayouts = iDescriptorSetLayouts.empty() ? nullptr : iDescriptorSetLayouts.data();
  layoutCreateInfo.pushConstantRangeCount = static_cast<uint32_t>(iPushConstantRanges.size());
  layoutCreateInfo.pPushConstantRanges = iPushConstantRanges.empty() ? nullptr : iPushConstantRanges.data();

  VkPipelineLayout layout = VK_NULL_HANDLE;
  VK_CHECK(vkCreatePipelineLayout(iDevice, &layoutCreateInfo, nullptr, &layout));
  if (!iDebugName.empty()) {
    vk_utils::setDebugObjectName(iDevice, layout, iDebugName.c_str());
  }
  return layout;
}

void Pipeline::destroyPipelineLayout(VkDevice iDevice, VkPipelineLayout iPipelineLayout) {
  if (iDevice != VK_NULL_HANDLE && iPipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(iDevice, iPipelineLayout, nullptr);
  }
}

void Pipeline::bind(VkCommandBuffer iCommandBuffer) {
  vkCmdBindPipeline(iCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mGraphicsPipeline);
}

VkShaderModule Pipeline::createShaderModule(const std::string& iShaderName) {
  std::string fullPath = ne::fs::resolveShaderPath(iShaderName);
  std::ifstream file(fullPath, std::ios::ate | std::ios::binary);
  NE_ASSERT(file.is_open(), "failed to open file: {}", fullPath);

  std::vector<char> fileBuffer(file.tellg());
  file.seekg(0, std::ios::beg);
  file.read(fileBuffer.data(), static_cast<std::streamsize>(fileBuffer.size()));
  file.close();

  VkShaderModuleCreateInfo shaderModuleCreateInfo{};
  shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shaderModuleCreateInfo.codeSize = fileBuffer.size();
  shaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(fileBuffer.data());

  VkShaderModule shaderModule;
  VK_CHECK(vkCreateShaderModule(mDevice, &shaderModuleCreateInfo, nullptr, &shaderModule));

  NE_VERBOSE("Loaded and created shader module: '{}' (Size: {})", iShaderName, vk_utils::formatBytes(fileBuffer.size()));
  return shaderModule;
}

} // namespace ne
