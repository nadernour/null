#pragma once

#include "core/ecs.h"
#include "core/math/transform.h"
#include "core/mesh_data.h"

// std
#include <memory>
#include <string>
#include <vector>

namespace ne {

class Mesh;
class Material;
class ResourceManager;

/**
 * @brief Reusable entity tree with its GPU meshes. Every instance shares the same meshes and materials,
 * so identical instances render as one instanced draw.
 */
class Prefab {
public:
  // Uploads the meshes; the ModelData is not referenced afterwards
  Prefab(ResourceManager& ioResourceManager, const ModelData& iModelData);

  // Returns the new root entity, which is named after the prefab
  Entity instantiate(Registry& ioRegistry, const Transform& iRootTransform = Transform{}) const;

private:
  std::string mName;
  std::vector<std::shared_ptr<Mesh>> mMeshes; // Indexed by ModelNode::meshIndex
  std::shared_ptr<Material> mMaterial;        // Shared by every submesh until glTF materials are imported
  std::vector<ModelNode> mNodes;              // Parents always precede their children
};

} // namespace ne
