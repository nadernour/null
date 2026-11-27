#include "scene/prefab.h"
#include "components/mesh_component.h"
#include "components/name_component.h"
#include "components/transform_component.h"
#include "core/assert.h"
#include "renderer/mesh.h"
#include "renderer/resource_manager.h"
#include "scene/transform_system.h"

// std
#include <format>
#include <span>

namespace ne {

namespace {

// iTextureIds holds the texture made from each of the model's images, which iMaterial's slots index
MaterialData toMaterialData(const ModelMaterial& iMaterial, std::span<const uint32_t> iTextureIds) {
  const auto toSlot = [&](const ModelTexture& iTexture) {
    NE_ASSERT(iTexture.image < static_cast<int32_t>(iTextureIds.size()), "Material '{}' uses image {} of a model with {} images",
              iMaterial.name, iTexture.image, iTextureIds.size());
    if (iTexture.image < 0) {
      return TextureSlot{};
    }
    return TextureSlot{.texture = iTextureIds[iTexture.image], .sampler = iTexture.sampler};
  };
  return MaterialData{
      .name = iMaterial.name,
      .params = iMaterial.params,
      .baseColorTexture = toSlot(iMaterial.baseColorTexture),
      .metallicRoughnessTexture = toSlot(iMaterial.metallicRoughnessTexture),
      .normalTexture = toSlot(iMaterial.normalTexture),
      .occlusionTexture = toSlot(iMaterial.occlusionTexture),
      .emissiveTexture = toSlot(iMaterial.emissiveTexture),
  };
}

} // namespace

Prefab::Prefab(ResourceManager& ioResourceManager, const ModelData& iModelData)
    : mName(iModelData.name), mNodes(iModelData.nodes) {
  std::vector<uint32_t> textureIds;
  textureIds.reserve(iModelData.images.size());
  for (size_t i = 0; i < iModelData.images.size(); ++i) {
    textureIds.push_back(ioResourceManager.createTexture(iModelData.images[i], std::format("{}_Image{}", mName, i)));
  }

  std::vector<std::shared_ptr<Material>> materials;
  materials.reserve(iModelData.materials.size());
  for (const ModelMaterial& material : iModelData.materials) {
    materials.push_back(ioResourceManager.createMaterial(toMaterialData(material, textureIds)));
  }

  mMeshComponents.reserve(iModelData.meshes.size());
  for (const MeshData& mesh : iModelData.meshes) {
    MeshComponent& meshComponent = mMeshComponents.emplace_back();
    meshComponent.mesh = ioResourceManager.createMesh(mesh);
    for (const SubmeshData& submesh : mesh.submeshes) {
      NE_ASSERT(submesh.materialIndex < static_cast<int32_t>(materials.size()),
                "Submesh references a material outside the prefab");
      meshComponent.materials.push_back(submesh.materialIndex >= 0 ? materials[submesh.materialIndex]
                                                                   : ioResourceManager.getDefaultMaterial());
    }
  }
}

Entity Prefab::instantiate(Registry& ioRegistry, const Transform& iRootTransform) const {
  Entity root = ioRegistry.createEntity();
  ioRegistry.addComponent<TransformComponent>(root, iRootTransform);
  ioRegistry.addComponent<NameComponent>(root, mName);

  std::vector<Entity> nodeEntities;
  nodeEntities.reserve(mNodes.size());

  for (const ModelNode& node : mNodes) {
    Entity entity = ioRegistry.createEntity();
    ioRegistry.addComponent<TransformComponent>(entity, node.localTransform);
    ioRegistry.addComponent<NameComponent>(entity, node.name);

    if (node.meshIndex >= 0) {
      NE_ASSERT(node.meshIndex < static_cast<int32_t>(mMeshComponents.size()), "Node references a mesh outside the prefab");
      // A mesh rejected by ResourceManager::createMesh() is null; its node keeps only its transform and name
      const MeshComponent& meshComponent = mMeshComponents[node.meshIndex];
      if (meshComponent.mesh) {
        ioRegistry.addComponent<MeshComponent>(entity, meshComponent);
      }
    }

    NE_ASSERT(node.parentIndex < static_cast<int32_t>(nodeEntities.size()), "Prefab nodes must list parents before children");
    TransformSystem::setParent(ioRegistry, entity, node.parentIndex >= 0 ? nodeEntities[node.parentIndex] : root);

    nodeEntities.push_back(entity);
  }

  return root;
}

} // namespace ne
