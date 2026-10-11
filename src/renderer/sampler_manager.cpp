#include "renderer/sampler_manager.h"
#include "core/assert.h"
#include "core/logger.h"
#include "renderer/device.h"
#include "renderer/utils.h"

namespace ne {

namespace {

// Vulkan guarantees 16x wherever samplerAnisotropy is supported, which Device requires
constexpr float kMaxAnisotropy = 16.0f;

struct SamplerConfig {
  SamplerType type;
  VkFilter filter;
  VkSamplerMipmapMode mipmapMode;
  VkSamplerAddressMode addressMode;
  bool anisotropic = false; // At kMaxAnisotropy
  bool compare = false;     // Reverse-Z depth comparison, for shadow maps
  const char* debugName = "";
};

constexpr std::array kSamplerConfigs{
    SamplerConfig{
        .type = SamplerType::LinearRepeat,
        .filter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropic = true,
        .debugName = "Sampler_LinearRepeat",
    },
    SamplerConfig{
        .type = SamplerType::LinearClamp,
        .filter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .anisotropic = true,
        .debugName = "Sampler_LinearClamp",
    },
    SamplerConfig{
        .type = SamplerType::LinearMirror,
        .filter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressMode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        .anisotropic = true,
        .debugName = "Sampler_LinearMirror",
    },
    SamplerConfig{
        .type = SamplerType::NearestClamp,
        .filter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .debugName = "Sampler_NearestClamp",
    },
    SamplerConfig{
        .type = SamplerType::NearestRepeat,
        .filter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .debugName = "Sampler_NearestRepeat",
    },
    SamplerConfig{
        .type = SamplerType::Shadow,
        .filter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .compare = true,
        .debugName = "Sampler_ShadowReverseZ",
    },
};
static_assert(kSamplerConfigs.size() == static_cast<size_t>(SamplerType::Count), "Every SamplerType needs a config");

VkSampler createSampler(VkDevice iDevice, const SamplerConfig& iConfig) {
  VkSamplerCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.magFilter = iConfig.filter;
  info.minFilter = iConfig.filter;
  info.mipmapMode = iConfig.mipmapMode;
  info.addressModeU = iConfig.addressMode;
  info.addressModeV = iConfig.addressMode;
  info.addressModeW = iConfig.addressMode;
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
  for (const SamplerConfig& config : kSamplerConfigs) {
    VkSampler& sampler = mSamplers[static_cast<size_t>(config.type)];
    NE_ASSERT(sampler == VK_NULL_HANDLE, "Two configs for sampler '{}'", config.debugName);
    sampler = createSampler(mDevice, config);
  }

  NE_LOG("Initialized SamplerManager: {} standard samplers created", mSamplers.size());
}

SamplerManager::~SamplerManager() {
  for (VkSampler sampler : mSamplers) {
    vkDestroySampler(mDevice, sampler, nullptr);
  }
  NE_LOG("Destroyed SamplerManager: cleaned up standard samplers");
}

VkSampler SamplerManager::get(SamplerType iType) const {
  NE_ASSERT(static_cast<size_t>(iType) < mSamplers.size(), "Sampler type index out of bounds");
  return mSamplers[static_cast<size_t>(iType)];
}

} // namespace ne
