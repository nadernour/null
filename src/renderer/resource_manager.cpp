#include "renderer/resource_manager.h"
#include "core/assert.h"
#include "core/image_data.h"
#include "core/image_utils.h"
#include "core/logger.h"
#include "core/material_data.h"
#include "core/mesh_data.h"
#include "renderer/geometry_allocator.h"
#include "renderer/gpu_types.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/rhi/bindless_manager.h"
#include "renderer/rhi/buffer.h"
#include "renderer/rhi/device.h"
#include "renderer/rhi/image.h"
#include "renderer/rhi/pipeline.h"
#include "renderer/rhi/sampler_manager.h"
#include "renderer/rhi/staging_manager.h"
#include "renderer/rhi/utils.h"

namespace ne {

namespace {

// VK_FORMAT_UNDEFINED when iImage's channel count has none. Half floats get the formats whose linear filtering Vulkan
// requires, which RGB16F's isn't.
VkFormat getTextureFormat(const ImageData& iImage) {
  if (iImage.channelType == ImageData::ChannelType::Unorm8) {
    return iImage.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  }
  NE_ASSERT(!iImage.srgb, "Half-float images are linear");
  switch (iImage.channels) {
    case 1:
      return VK_FORMAT_R16_SFLOAT;
    case 2:
      return VK_FORMAT_R16G16_SFLOAT;
    case 4:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    default:
      return VK_FORMAT_UNDEFINED;
  }
}

uint32_t packTexture(const TextureSlot& iSlot) {
  return gpu::packTexture(iSlot.texture, SamplerManager::getTextureSamplerIndex(iSlot.sampler));
}

} // namespace

ResourceManager::ResourceManager(Device* iDevice) : mDevice(iDevice) {
  NE_ASSERT(mDevice);
  mSamplerManager = std::make_unique<SamplerManager>(mDevice);
  mStagingManager = std::make_unique<StagingManager>(mDevice);
  mGeometryAllocator = std::make_unique<GeometryAllocator>(mDevice, vk_utils::kVertexPoolSize, vk_utils::kIndexPoolSize);
  mBindlessManager = std::make_unique<BindlessManager>(mDevice->getDevice(), mSamplerManager.get());

  VkDescriptorSetLayout setLayout = mBindlessManager->getDescriptorSetLayout();
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = gpu::kPushConstantStages;
  pushConstantRange.offset = 0;
  pushConstantRange.size = gpu::kPushConstantsSize;

  mPipelineLayout =
      Pipeline::createPipelineLayout(mDevice->getDevice(), {setLayout}, {pushConstantRange}, "Bindless_PipelineLayout");

  // Textures a material lacks resolve to kFallbackTextureIndex, so the white texture must be created first
  ImageData whiteData = ImageData::createWhite1x1();
  uint32_t defaultTexIdx = createTexture(whiteData, "Default_White_Texture");
  NE_ASSERT(defaultTexIdx == kFallbackTextureIndex, "Fallback white texture must occupy bindless index kFallbackTextureIndex");

  Buffer::Config materialBufferConfig{
      .size = vk_utils::kMaxMaterials * sizeof(gpu::Material),
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      .memoryUsage = MemoryUsage::DeviceLocal,
      .debugName = "ResourceManager_MaterialBuffer",
  };
  mMaterialBuffer = std::make_unique<Buffer>(mDevice, materialBufferConfig);
  mDefaultMaterial = createMaterial(MaterialData{.name = "Default"});
  NE_ASSERT(mDefaultMaterial->getMaterialIndex() == 0, "The default material must be material 0");
}

ResourceManager::~ResourceManager() {
  // The staging manager may still flush copies into the textures and the material buffer, so it goes first
  mStagingManager.reset();
  mTextures.clear();
  mDefaultMaterial.reset();
  Pipeline::destroyPipelineLayout(mDevice->getDevice(), mPipelineLayout);
  mMaterialBuffer.reset();
  mBindlessManager.reset();
  mGeometryAllocator.reset();
  mSamplerManager.reset();
}

std::shared_ptr<Material> ResourceManager::createMaterial(const MaterialData& iData) {
  static_assert(TextureSlot{}.texture == kFallbackTextureIndex, "An empty texture slot must hold the fallback");
  if (mMaterialCount == vk_utils::kMaxMaterials) {
    NE_ERROR("ResourceManager: Material '{}' has no slot left in the material buffer (all {} are taken); using the default "
             "material",
             iData.name, vk_utils::kMaxMaterials);
    return mDefaultMaterial;
  }

  const MaterialParams& params = iData.params;
  const gpu::Material material{
      .baseColor = params.baseColor,
      .emissive = params.emissive,
      .metallic = params.metallic,
      .roughness = params.roughness,
      .normalScale = params.normalScale,
      .occlusionStrength = params.occlusionStrength,
      .alphaCutoff = params.alphaCutoff,
      .baseColorTexture = packTexture(iData.baseColorTexture),
      .metallicRoughnessTexture = packTexture(iData.metallicRoughnessTexture),
      .normalTexture = packTexture(iData.normalTexture),
      .occlusionTexture = packTexture(iData.occlusionTexture),
      .emissiveTexture = packTexture(iData.emissiveTexture),
      // A normal texture that fell back to white would read as a (1, 1, 1) normal map
      .flags = iData.normalTexture.texture != kFallbackTextureIndex ? gpu::kMaterialHasNormalTexture : 0u,
  };

  if (!mStagingManager->isBatching()) {
    mStagingManager->beginBatch();
  }
  const uint32_t materialIndex = mMaterialCount++;
  mStagingManager->stageBufferCopy(mMaterialBuffer->getBuffer(), &material, sizeof(gpu::Material),
                                   materialIndex * sizeof(gpu::Material));
  NE_VERBOSE("ResourceManager: Material '{}' takes slot {} of the material buffer", iData.name, materialIndex);
  return std::make_shared<Material>(materialIndex, params.alphaMode, params.doubleSided);
}

VkDeviceAddress ResourceManager::getMaterialBufferAddress() const { return mMaterialBuffer->getDeviceAddress(); }

std::shared_ptr<Mesh> ResourceManager::createMesh(const MeshData& iMeshData) {
  // Malformed data is rejected before anything is staged. The vertex shader fetches vertices through a raw device
  // address, so an index past its submesh's vertices would read another mesh's vertices or fault the GPU.
  if (!iMeshData.validate()) {
    return nullptr;
  }

  if (!mStagingManager->isBatching()) {
    mStagingManager->beginBatch();
  }
  std::optional<Mesh> mesh = mGeometryAllocator->stageMesh(*mStagingManager, iMeshData);
  if (!mesh) {
    return nullptr;
  }
  return std::make_shared<Mesh>(std::move(*mesh));
}

uint32_t ResourceManager::createTexture(const ImageData& iImageData, const std::string& iDebugName) {
  // Checked before anything is created, so a rejected texture costs no GPU memory
  const std::string name = iDebugName.empty() ? "Texture" : iDebugName;
  const uint32_t maxDimension = mDevice->getPhysicalDeviceProperties().limits.maxImageDimension2D;
  if (iImageData.width > maxDimension || iImageData.height > maxDimension) {
    NE_ERROR("ResourceManager: Texture '{}' is {}x{}, past the GPU's {}x{} limit; using the white fallback", name,
             iImageData.width, iImageData.height, maxDimension, maxDimension);
    return kFallbackTextureIndex;
  }
  if (mBindlessManager->isFull()) {
    NE_ERROR("ResourceManager: Texture '{}' has no bindless slot left (all {} are taken); using the white fallback", name,
             mBindlessManager->getMaxSampledImages());
    return kFallbackTextureIndex;
  }
  const VkFormat format = getTextureFormat(iImageData);
  if (format == VK_FORMAT_UNDEFINED) {
    NE_ERROR("ResourceManager: Half-float texture '{}' has {} channels, not 1, 2, or 4; using the white fallback", name,
             iImageData.channels);
    return kFallbackTextureIndex;
  }

  if (!mStagingManager->isBatching()) {
    mStagingManager->beginBatch();
  }
  // 8-bit images get a mip chain; half-float images upload as they are
  const bool isHalf = iImageData.channelType == ImageData::ChannelType::Float16;
  const ImageData mipChain = isHalf ? ImageData{} : image_utils::generateMipChain(iImageData);
  const ImageData& upload = isHalf ? iImageData : mipChain;
  Image::Config config{
      .width = upload.width,
      .height = upload.height,
      .format = format,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .mipLevels = upload.mipLevels,
      .debugName = name,
  };
  auto image = std::make_unique<Image>(mDevice, config);
  mStagingManager->stageImageUpload(*image, upload.pixels.data(), upload.pixels.size());

  uint32_t textureId = mBindlessManager->registerSampledImage(image->getImageView());
  mTextures.push_back(std::move(image));
  return textureId;
}

void ResourceManager::flushUploads() {
  if (mStagingManager && mStagingManager->hasPendingUploads()) {
    mStagingManager->endBatch();
  }
}

} // namespace ne
