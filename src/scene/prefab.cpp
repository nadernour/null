#include "scene/prefab.h"
#include "components/mesh_component.h"
#include "components/name_component.h"
#include "components/transform_component.h"
#include "core/assert.h"
#include "renderer/mesh.h"
#include "renderer/resource_manager.h"
#include "scene/transform_system.h"

namespace ne {

Prefab::Prefab(ResourceManager& ioResourceManager, const ModelData& iModelData)
    : mName(iModelData.name), mMaterial(ioResourceManager.createMaterial()), mNodes(iModelData.nodes) {
  mMeshes.reserve(iModelData.meshes.size());
  for (const MeshData& mesh : iModelData.meshes) {
    mMeshes.push_back(ioResourceManager.createMesh(mesh));
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
      NE_ASSERT(node.meshIndex < static_cast<int32_t>(mMeshes.size()), "Node references a mesh outside the prefab");
      const std::shared_ptr<Mesh>& mesh = mMeshes[node.meshIndex];
      ioRegistry.addComponent<MeshComponent>(entity, mesh, std::vector(mesh->getSubmeshes().size(), mMaterial));
    }

    NE_ASSERT(node.parentIndex < static_cast<int32_t>(nodeEntities.size()), "Prefab nodes must list parents before children");
    TransformSystem::setParent(ioRegistry, entity, node.parentIndex >= 0 ? nodeEntities[node.parentIndex] : root);

    nodeEntities.push_back(entity);
  }

  return root;
}

} // namespace ne
