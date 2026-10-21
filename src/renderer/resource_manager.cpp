#include "renderer/resource_manager.h"
#include "core/assert.h"
#include "core/image_data.h"
#include "core/image_utils.h"
#include "core/logger.h"
#include "core/mesh_data.h"
#include "renderer/bindless_manager.h"
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

ResourceManager::ResourceManager(Device* iDevice, VkFormat iDefaultColorFormat, VkFormat iDefaultDepthFormat)
    : mDevice(iDevice), mDefaultColorFormat(iDefaultColorFormat), mDefaultDepthFormat(iDefaultDepthFormat) {
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

  // New materials start on kFallbackTextureIndex, so the white texture must be created first
  ImageData whiteData = ImageData::createWhite1x1();
  uint32_t defaultTexIdx = createTexture(whiteData, "Default_White_Texture");
  NE_ASSERT(defaultTexIdx == kFallbackTextureIndex, "Fallback white texture must occupy bindless index kFallbackTextureIndex");
}

ResourceManager::~ResourceManager() {
  mStagingManager.reset();
  mTextures.clear();
  mPipelines.clear();
  Pipeline::destroyPipelineLayout(mDevice->getDevice(), mPipelineLayout);
  mBindlessManager.reset();
  mGeometryAllocator.reset();
  mSamplerManager.reset();
}

std::shared_ptr<Pipeline> ResourceManager::getOrCreatePipeline(const std::string& iShaderName) {
  const std::string& shaderName = iShaderName.empty() ? vk_utils::kDefaultShader : iShaderName;

  auto it = mPipelines.find(shaderName);
  if (it != mPipelines.end()) {
    return it->second;
  }

  Pipeline::Config config{
      .shaderName = shaderName,
      .layout = mPipelineLayout,
      .colorAttachmentFormat = mDefaultColorFormat,
      .depthAttachmentFormat = mDefaultDepthFormat,
  };

  auto pipeline = std::make_shared<Pipeline>(mDevice->getDevice(), config);
  mPipelines[shaderName] = pipeline;
  return pipeline;
}

std::shared_ptr<Material> ResourceManager::createMaterial(const std::string& iShaderName) {
  return std::make_shared<Material>(getOrCreatePipeline(iShaderName), kFallbackTextureIndex);
}

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
