#pragma once

#include <volk/volk.h>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ne {

class Device;
class StagingManager;
class GeometryAllocator;
class SamplerManager;
class BindlessManager;
class Buffer;
class Mesh;
class Image;
class Material;
struct ImageData;
struct MaterialData;
struct MeshData;

class ResourceManager {
public:
  // Bindless index of the white 1x1 texture, created first; textures a material lacks and rejected textures fall back to it
  static constexpr uint32_t kFallbackTextureIndex = 0;

  ResourceManager(Device* iDevice);
  ~ResourceManager();

  ResourceManager(const ResourceManager&) = delete;
  ResourceManager& operator=(const ResourceManager&) = delete;
  ResourceManager(ResourceManager&&) = delete;
  ResourceManager& operator=(ResourceManager&&) = delete;

  std::shared_ptr<Mesh> createMesh(const MeshData& iMeshData);
  uint32_t createTexture(const ImageData& iImageData, const std::string& iDebugName = "");
  // Writes the material into the next slot of the material buffer; iTextureIds maps each TextureRef::image to a
  // texture from createTexture(). The default material, with the reason logged, once the buffer is full.
  std::shared_ptr<Material> createMaterial(const MaterialData& iData, std::span<const uint32_t> iTextureIds = {});
  void flushUploads();

  StagingManager* getStagingManager() const { return mStagingManager.get(); }
  GeometryAllocator* getGeometryAllocator() const { return mGeometryAllocator.get(); }
  SamplerManager* getSamplerManager() const { return mSamplerManager.get(); }
  BindlessManager* getBindlessManager() const { return mBindlessManager.get(); }
  // Shared by every graphics pipeline: set 0 is the bindless set, plus one push-constant range
  VkPipelineLayout getPipelineLayout() const { return mPipelineLayout; }
  // Material 0, from MaterialData{}: the engine's dielectric
  const std::shared_ptr<Material>& getDefaultMaterial() const { return mDefaultMaterial; }
  // The material buffer never moves, so its address holds for the ResourceManager's lifetime
  VkDeviceAddress getMaterialBufferAddress() const;

private:
  Device* mDevice = nullptr;

  std::unique_ptr<StagingManager> mStagingManager;
  std::unique_ptr<GeometryAllocator> mGeometryAllocator;
  std::unique_ptr<SamplerManager> mSamplerManager;
  std::unique_ptr<BindlessManager> mBindlessManager;

  VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
  std::vector<std::unique_ptr<Image>> mTextures;

  // Append-only: a slot is written once, so no frame in flight ever sees it change
  std::unique_ptr<Buffer> mMaterialBuffer;
  uint32_t mMaterialCount = 0;
  std::shared_ptr<Material> mDefaultMaterial;
};

} // namespace ne
