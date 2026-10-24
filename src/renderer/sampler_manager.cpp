#include "renderer/sampler_manager.h"
#include "core/assert.h"
#include "core/logger.h"
#include "renderer/device.h"
#include "renderer/utils.h"

#include <format>
#include <string>

namespace ne {

namespace {

// Vulkan guarantees 16x wherever samplerAnisotropy is supported, which Device requires
constexpr float kMaxAnisotropy = 16.0f;

constexpr std::array kTextureFilters{TextureFilter::Linear, TextureFilter::Nearest};
constexpr std::array kTextureWraps{TextureWrap::Repeat, TextureWrap::Clamp, TextureWrap::Mirror};
static_assert(kTextureFilters.size() == SamplerManager::kTextureFilterCount, "kTextureFilters must list every TextureFilter");
static_assert(kTextureWraps.size() == SamplerManager::kTextureWrapCount, "kTextureWraps must list every TextureWrap");

struct SamplerConfig {
  VkFilter filter;
  VkSamplerMipmapMode mipmapMode;
  VkSamplerAddressMode addressModeU;
  VkSamplerAddressMode addressModeV;
  bool anisotropic = false; // At kMaxAnisotropy
  bool compare = false;     // Reverse-Z depth comparison, for shadow maps
  std::string debugName;
};

VkSamplerAddressMode toAddressMode(TextureWrap iWrap) {
  switch (iWrap) {
    case TextureWrap::Repeat:
      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case TextureWrap::Clamp:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case TextureWrap::Mirror:
      return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case TextureWrap::Count:
      break;
  }
  return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

const char* toString(TextureWrap iWrap) {
  switch (iWrap) {
    case TextureWrap::Repeat:
      return "Repeat";
    case TextureWrap::Clamp:
      return "Clamp";
    case TextureWrap::Mirror:
      return "Mirror";
    case TextureWrap::Count:
      break;
  }
  return "Unknown";
}

// Linear samplers filter trilinearly with anisotropy; nearest ones pick the nearest texel and mip level
SamplerConfig makeTextureSamplerConfig(TextureFilter iFilter, TextureWrap iWrapU, TextureWrap iWrapV) {
  const bool isLinear = iFilter == TextureFilter::Linear;
  return SamplerConfig{
      .filter = isLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
      .mipmapMode = isLinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = toAddressMode(iWrapU),
      .addressModeV = toAddressMode(iWrapV),
      .anisotropic = isLinear,
      .debugName = std::format("Sampler_{}_{}_{}", isLinear ? "Linear" : "Nearest", toString(iWrapU), toString(iWrapV)),
  };
}

VkSampler createSampler(VkDevice iDevice, const SamplerConfig& iConfig) {
  VkSamplerCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.magFilter = iConfig.filter;
  info.minFilter = iConfig.filter;
  info.mipmapMode = iConfig.mipmapMode;
  info.addressModeU = iConfig.addressModeU;
  info.addressModeV = iConfig.addressModeV;
  info.addressModeW = iConfig.addressModeU;
  info.mipLodBias = 0.0f;
  info.anisotropyEnable = iConfig.anisotropic ? VK_TRUE : VK_FALSE;
  info.maxAnisotropy = iConfig.anisotropic ? kMaxAnisotropy : 1.0f;
  info.compareEnable = iConfig.compare ? VK_TRUE : VK_FALSE;
  info.compareOp = iConfig.compare ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_ALWAYS;
  info.minLod = 0.0f;
  info.maxLod = VK_LOD_CLAMP_NONE; // The image view decides which levels exist
  info.borderColor = iConfig.compare ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK : VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  info.unnormalizedCoordinates = VK_FALSE;

  VkSampler sampler = VK_NULL_HANDLE;
  VK_CHECK(vkCreateSampler(iDevice, &info, nullptr, &sampler));
  vk_utils::setDebugObjectName(iDevice, sampler, iConfig.debugName);
  return sampler;
}

} // namespace

SamplerManager::SamplerManager(Device* iDevice) : mDevice(iDevice->getDevice()) {
  // Every slot is filled through getTextureSamplerIndex, so the shaders and this table agree on the order
  for (TextureFilter filter : kTextureFilters) {
    for (TextureWrap wrapU : kTextureWraps) {
      for (TextureWrap wrapV : kTextureWraps) {
        const SamplerConfig config = makeTextureSamplerConfig(filter, wrapU, wrapV);
        VkSampler& sampler = mSamplers[getTextureSamplerIndex(filter, wrapU, wrapV)];
        NE_ASSERT(sampler == VK_NULL_HANDLE, "Two texture samplers share the slot of '{}'", config.debugName);
        sampler = createSampler(mDevice, config);
      }
    }
  }

  mSamplers[kShadowSamplerIndex] = createSampler(mDevice, SamplerConfig{
                                                              .filter = VK_FILTER_LINEAR,
                                                              .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                                              .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                                              .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                                              .compare = true,
                                                              .debugName = "Sampler_ShadowReverseZ",
                                                          });

  NE_VERBOSE("Initialized SamplerManager: {} samplers created", mSamplers.size());
}

SamplerManager::~SamplerManager() {
  for (VkSampler sampler : mSamplers) {
    vkDestroySampler(mDevice, sampler, nullptr);
  }
  NE_VERBOSE("Destroyed SamplerManager: cleaned up {} samplers", mSamplers.size());
}

} // namespace ne
