#include "renderer/resource_manager.h"
#include "core/assert.h"
#include "core/defines.h"
#include "core/image_data.h"
#include "core/image_utils.h"
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
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(gpu::PushConstants);

  mScenePipelineLayout =
      Pipeline::createPipelineLayout(mDevice->getDevice(), {setLayout}, {pushConstantRange}, "Scene_PipelineLayout");

  // Materials default to texture index 0, so it must be the white fallback
  ImageData whiteData = ImageData::createWhite1x1();
  uint32_t defaultTexIdx = createTexture(whiteData, true, "Default_White_Texture");
  NE_ASSERT(defaultTexIdx == 0, "Fallback white texture must occupy bindless texture index 0");
}

ResourceManager::~ResourceManager() {
  mStagingManager.reset();
  mTextures.clear();
  mPipelines.clear();
  Pipeline::destroyPipelineLayout(mDevice->getDevice(), mScenePipelineLayout);
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
      .layout = mScenePipelineLayout,
      .colorAttachmentFormat = mDefaultColorFormat,
      .depthAttachmentFormat = mDefaultDepthFormat,
  };

  auto pipeline = std::make_shared<Pipeline>(mDevice->getDevice(), config);
  mPipelines[shaderName] = pipeline;
  return pipeline;
}

std::shared_ptr<Material> ResourceManager::createMaterial(const std::string& iShaderName) {
  return std::make_shared<Material>(getOrCreatePipeline(iShaderName));
}

std::shared_ptr<Mesh> ResourceManager::createMesh(const MeshData& iMeshData) {
  if (!mStagingManager->isBatching()) {
    mStagingManager->beginBatch();
  }
  std::vector<Mesh::Submesh> submeshes;
  submeshes.reserve(iMeshData.mSubmeshes.size());
  for (const SubmeshData& submesh : iMeshData.mSubmeshes) {
    submeshes.push_back(mGeometryAllocator->stageSubmesh(*mStagingManager, submesh));
  }
  return std::make_shared<Mesh>(std::move(submeshes));
}

uint32_t ResourceManager::createTexture(const ImageData& iImageData, bool iSrgb, const std::string& iDebugName) {
  if (!mStagingManager->isBatching()) {
    mStagingManager->beginBatch();
  }
  const ImageData mipChain = ImageUtils::generateMipChain(iImageData, iSrgb);
  Image::Config config{
      .width = mipChain.mWidth,
      .height = mipChain.mHeight,
      .format = iSrgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .mipLevels = mipChain.mMipLevels,
      .debugName = iDebugName.empty() ? "Texture" : iDebugName,
  };
  auto image = std::make_unique<Image>(mDevice, config);
  mStagingManager->stageImageUpload(*image, mipChain.mPixels.data(), mipChain.mPixels.size());

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
