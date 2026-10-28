#include "renderer/resource_manager.h"
#include "core/assert.h"
#include "core/image_data.h"
#include "core/image_utils.h"
#include "core/logger.h"
#include "core/material_data.h"
#include "core/mesh_data.h"
#include "renderer/bindless_manager.h"
#include "renderer/buffer.h"
#include "renderer/device.h"
#include "renderer/geometry_allocator.h"
#include "renderer/gpu_types.h"
#include "renderer/image.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/pipeline.h"
#include "renderer/sampler_manager.h"
#include "renderer/staging_manager.h"
#include "renderer/utils.h"

namespace ne {

namespace {

// A material's texture as the shader reads it: the bindless index (low 16 bits) and the sampler index (high 16 bits)
uint32_t packTexture(uint32_t iTextureId, const TextureRef& iTexture) {
  static_assert(vk_utils::kMaxSampledImages <= 0x10000, "Bindless indices must fit in 16 bits");
  static_assert(SamplerManager::kSamplerCount <= 0x10000, "Sampler indices must fit in 16 bits");
  return iTextureId | (SamplerManager::getTextureSamplerIndex(iTexture.filter, iTexture.wrapU, iTexture.wrapV) << 16);
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

std::shared_ptr<Material> ResourceManager::createMaterial(const MaterialData& iData, std::span<const uint32_t> iTextureIds) {
  if (mMaterialCount == vk_utils::kMaxMaterials) {
    NE_ERROR("ResourceManager: Material '{}' has no slot left in the material buffer (all {} are taken); using the default "
             "material",
             iData.name, vk_utils::kMaxMaterials);
    return mDefaultMaterial;
  }

  const auto resolveTexture = [&](const TextureRef& iTexture) {
    NE_ASSERT(iTexture.image < static_cast<int32_t>(iTextureIds.size()),
              "Material '{}' uses image {}, but only {} texture ids were given", iData.name, iTexture.image, iTextureIds.size());
    return iTexture.image >= 0 ? iTextureIds[iTexture.image] : kFallbackTextureIndex;
  };
  // A normal texture that fell back to white would read as a (1, 1, 1) normal map
  const uint32_t normalTextureId = resolveTexture(iData.normalTexture);
  const gpu::Material material{
      .baseColor = iData.baseColor,
      .emissive = iData.emissive,
      .metallic = iData.metallic,
      .roughness = iData.roughness,
      .normalScale = iData.normalScale,
      .occlusionStrength = iData.occlusionStrength,
      .alphaCutoff = iData.alphaCutoff,
      .baseColorTexture = packTexture(resolveTexture(iData.baseColorTexture), iData.baseColorTexture),
      .metallicRoughnessTexture = packTexture(resolveTexture(iData.metallicRoughnessTexture), iData.metallicRoughnessTexture),
      .normalTexture = packTexture(normalTextureId, iData.normalTexture),
      .occlusionTexture = packTexture(resolveTexture(iData.occlusionTexture), iData.occlusionTexture),
      .emissiveTexture = packTexture(resolveTexture(iData.emissiveTexture), iData.emissiveTexture),
      .flags = normalTextureId != kFallbackTextureIndex ? gpu::kMaterialHasNormalTexture : 0u,
  };

  if (!mStagingManager->isBatching()) {
    mStagingManager->beginBatch();
  }
  const uint32_t materialIndex = mMaterialCount++;
  mStagingManager->stageBufferCopy(mMaterialBuffer->getBuffer(), &material, sizeof(gpu::Material),
                                   materialIndex * sizeof(gpu::Material));
  NE_VERBOSE("ResourceManager: Material '{}' takes slot {} of the material buffer", iData.name, materialIndex);
  return std::make_shared<Material>(materialIndex, iData.alphaMode, iData.doubleSided);
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

  if (!mStagingManager->isBatching()) {
    mStagingManager->beginBatch();
  }
  const ImageData mipChain = ImageUtils::generateMipChain(iImageData);
  Image::Config config{
      .width = mipChain.width,
      .height = mipChain.height,
      .format = mipChain.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .mipLevels = mipChain.mipLevels,
      .debugName = name,
  };
  auto image = std::make_unique<Image>(mDevice, config);
  mStagingManager->stageImageUpload(*image, mipChain.pixels.data(), mipChain.pixels.size());

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
