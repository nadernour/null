#pragma once

#include <volk/volk.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ne {

class Device;
class StagingManager;
class GeometryAllocator;
class SamplerManager;
class BindlessManager;
class Mesh;
class Image;
class Pipeline;
class Material;
struct ImageData;
struct MeshData;

class ResourceManager {
public:
  ResourceManager(Device* iDevice, VkFormat iDefaultColorFormat, VkFormat iDefaultDepthFormat);
  ~ResourceManager();

  ResourceManager(const ResourceManager&) = delete;
  ResourceManager& operator=(const ResourceManager&) = delete;
  ResourceManager(ResourceManager&&) = delete;
  ResourceManager& operator=(ResourceManager&&) = delete;

  std::shared_ptr<Mesh> createMesh(const MeshData& iMeshData);
  uint32_t createTexture(const ImageData& iImageData, const std::string& iDebugName = "");
  std::shared_ptr<Pipeline> getOrCreatePipeline(const std::string& iShaderName = "");
  std::shared_ptr<Material> createMaterial(const std::string& iShaderName = "");
  void flushUploads();

  StagingManager* getStagingManager() const { return mStagingManager.get(); }
  GeometryAllocator* getGeometryAllocator() const { return mGeometryAllocator.get(); }
  SamplerManager* getSamplerManager() const { return mSamplerManager.get(); }
  BindlessManager* getBindlessManager() const { return mBindlessManager.get(); }
  VkPipelineLayout getScenePipelineLayout() const { return mScenePipelineLayout; }

private:
  Device* mDevice = nullptr;
  VkFormat mDefaultColorFormat = VK_FORMAT_UNDEFINED;
  VkFormat mDefaultDepthFormat = VK_FORMAT_UNDEFINED;

  std::unique_ptr<StagingManager> mStagingManager;
  std::unique_ptr<GeometryAllocator> mGeometryAllocator;
  std::unique_ptr<SamplerManager> mSamplerManager;
  std::unique_ptr<BindlessManager> mBindlessManager;

  VkPipelineLayout mScenePipelineLayout = VK_NULL_HANDLE;
  std::unordered_map<std::string, std::shared_ptr<Pipeline>> mPipelines;
  std::vector<std::unique_ptr<Image>> mTextures;
};

} // namespace ne
